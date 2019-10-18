#!/usr/bin/python3
import os
import subprocess
import hashlib
import collections
import random
import string
import signal
import time
import sys
import datetime

_DEFAULTS = {
	"hdrbackup_file":		"data/backup.img",
	"resume_file":			"data/resume.bin",
	"key_file":				"data/keyfile.bin",
}

class LUKSIPCTest(object):
	_PreTestParameters = collections.namedtuple("PreTestParameters", [ "seed", "plain_data_hash", "backup_header_hash", "source", "expected_sizediff", "devsize_pre", "devsize_post" ])
	def __init__(self, testengine, assumptions):
		self._engine = testengine
		self._assumptions = assumptions

	def __getitem__(self, key):
		return self._assumptions[key]

	def run(self):
		raise Exception(NotImplemented)

	def prepare_device(self, expected_sizediff = None):
		"""Prepare a plain device (no LUKS) with a PRNG pattern. Hash the
		device excluding the trailing part that is going to be cut away by
		LUKSification and also hash the part that is going to be in the header
		backup (128 MiB). Typically called to test LUKSification."""
		if expected_sizediff is None:
			expected_sizediff = self["default_luks_hdr_size"]

		devsize_pre = self._engine.rawdevsize
		devsize_post = devsize_pre + expected_sizediff

		seed = random.randint(0, 0xffffffff)
		plain_data_hash = self._engine.patternize_rawdev(expected_sizediff, seed)
		backup_header_hash = self._engine.hash_rawdev(total_size = self["default_backup_hdr_size"])
		return self._PreTestParameters(seed = seed, plain_data_hash = plain_data_hash, backup_header_hash = backup_header_hash, source = "plain", expected_sizediff = expected_sizediff, devsize_pre = devsize_pre, devsize_post = devsize_post)

	def prepare_luksdevice(self, luksformat_params = None, expected_sizediff = 0):
		"""Prepare a LUKS device and fill the plain part of the device with a
		PRNG pattern.  Hash the whole plain data of the LUKS device container
		(unlocked) and hash the part of the raw device that is going to end up
		in the header backup (128 MiB). Typically called to test
		reLUKSification."""
		if luksformat_params is None:
			luksformat_params = [ ]

		seed = random.randint(0, 0xffffffff)
		self._engine.luksFormat(luksformat_params)
		try:
			container = self._engine.luksOpen()
			devsize_pre = self._engine._getsizeof(container.unlockedblkdev)
			plain_data_hash = self._engine.patternize_device(container.unlockedblkdev, seed = seed)
		finally:
			self._engine.luksClose(container)

		devsize_post = devsize_pre + expected_sizediff
		backup_header_hash = self._engine.hash_rawdev(total_size = self["default_backup_hdr_size"])
		return self._PreTestParameters(seed = seed, plain_data_hash = plain_data_hash, backup_header_hash = backup_header_hash, source = "luks", expected_sizediff = expected_sizediff, devsize_pre = devsize_pre, devsize_post = devsize_post)

	def verify_container(self, pretestparams):
		"""Verify the container integrity against the parameters that were
		determined at generation from the prepare_xyz() function by checking
		the MD5SUM of the (unlocked) device."""
		self._engine.verify_file(_DEFAULTS["hdrbackup_file"], pretestparams.backup_header_hash)

		# Verify initial luksification worked by decrypting and verifying hash
		container = self._engine.luksOpen()
		try:
			self._engine.verify_device(container.unlockedblkdev, pretestparams.plain_data_hash)
		finally:
			self._engine.luksClose(container)

	def _assert(self, cond, msg):
		if not cond:
			raise Exception("Assertion failed: %s" % (msg))

class TestEngine(object):
	_OpenLUKSContainer = collections.namedtuple("OpenLUKSContainer", [ "rawdatablkdev", "unlockedblkdev", "keyfile", "dmname" ])

	def __init__(self, destroy_data_dev, luksipc_binary, logdir, additional_params):
		self._destroy_dev = destroy_data_dev
		self._luksipc_bin = luksipc_binary
		self._logdir = logdir
		if not self._logdir.endswith("/"):
			self._logdir += "/"
		try:
			os.makedirs(self._logdir)
		except FileExistsError:
			pass
		try:
			os.makedirs("data/")
		except FileExistsError:
			pass
		self._patternizer_bin = "prng/prng_crc64"
		self._rawdevsize = self._getsizeof(self._destroy_dev)
		self._total_log = open(self._logdir + "summary.txt", "a")
		self._lastlogfile = self._get_lastlogfile()
		self._additional_params = additional_params

		self._kill_list = set()
		try:
			for line in open("kill_list.txt"):
				line = line.rstrip("\r\n").strip()
				if line.startswith("#") or line.startswith(";"):
					continue
				if line == "":
					continue
				self._kill_list.add(line)
		except FileNotFoundError:
			print("Warning: no kill list found.")
		if self._destroy_dev not in self._kill_list:
			raise Exception("The device you want to work with for testing purposes is not on the kill list, refusing to work with that device. Please add it to the file 'kill_list.txt' if you're okay with the irrevocable destruction of all data on it.")

	def _log(self, msg):
		msg = "%s: %s" % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg)
		print(msg)
		print(msg, file = self._total_log)
		self._total_log.flush()

	def _get_lastlogfile(self):
		lastlog = 0
		for filename in os.listdir(self._logdir):
			if not filename.endswith(".log"):
				continue
			lastlog = max(int(filename[:-4]), lastlog)
		return lastlog

	@property
	def rawdevsize(self):
		return self._rawdevsize

	@staticmethod
	def _randstr(length):
		return "".join(random.choice(string.ascii_lowercase) for i in range(length))

	@staticmethod
	def _getsizeof(blkdev):
		f = open(blkdev, "rb")
		f.seek(0, os.SEEK_END)
		devsize = f.tell()
		f.close()
		return devsize

	def _get_log_file(self, purpose):
		self._lastlogfile += 1
		filename = "%s%04d.log" % (self._logdir, self._lastlogfile)
		f = open(filename, "w")
		print("%s" % (purpose), file = f)
		print("=" * 120, file = f)
		self._log("Execute: %s -> %s" % (purpose, filename))
		f.flush()
		return f

	def hash_device(self, blkdevname, exclude_bytes = 0, total_size = None):
		if total_size is not None:
			hash_length = total_size
		else:
			hash_length = self._getsizeof(blkdevname) - exclude_bytes
		assert(hash_length >= 0)

		f = open(blkdevname, "rb")
		datahash = hashlib.md5()
		remaining = hash_length
		while remaining > 0:
			if remaining > 1024 * 1024:
				data = f.read(1024 * 1024)
			else:
				data = f.read(remaining)
			datahash.update(data)
			if len(data) == 0:
				break
			remaining -= len(data)
		datahash = datahash.hexdigest()
		self._log("Hash device %s (length %d): %s" % (blkdevname, hash_length, datahash))
		f.close()
		return datahash

	def hash_rawdev(self, exclude_bytes = 0, total_size = None):
		return self.hash_device(self._destroy_dev, exclude_bytes, total_size)

	def verify_device(self, blkdevname, expect_hash, exclude_bytes = 0, total_size = None):
		self._log("Verification of hash of block device %s" % (blkdevname))
		calc_hash = self.hash_device(blkdevname, exclude_bytes, total_size)
		if calc_hash == expect_hash:
			self._log("PASS: '%s' has the correct hash value (%s)." % (blkdevname, expect_hash))
		else:
			msg = "FAIL: %s is supposed to have hash %s, but had hash %s." % (blkdevname, expect_hash, calc_hash)
			self._log(msg)
			raise Exception(msg)

	def verify_file(self, filename, expect_hash):
		self._log("Verification of hash of file %s" % (filename))
		f = open(filename, "rb")
		datahash = hashlib.md5()
		while True:
			data = f.read(1024 * 1024)
			if len(data) == 0:
				break
			datahash.update(data)
		f.close()
		calc_hash = datahash.hexdigest()
		if calc_hash == expect_hash:
			self._log("PASS: '%s' has the correct hash value (%s)." % (filename, expect_hash))
		else:
			msg = "FAIL: %s is supposed to have hash %s, but had hash %s." % (filename, expect_hash, calc_hash)
			self._log(msg)
			raise Exception(msg)

	def verify_hdrbackup_file(self, expect_hash):
		return self.verify_file(_DEFAULTS["hdrbackup_file"], expect_hash)

	def scrub_device(self):
		self._log("Scrubbing raw device")
		self._execute_sync([ "dd", "if=/dev/zero", "of=" + self._destroy_dev, "bs=1M" ], success_codes = [ 1 ])

	def scrub_device_hdr(self):
		self._log("Scrubbing raw device header")
		self._execute_sync([ "dd", "if=/dev/zero", "of=" + self._destroy_dev, "bs=1M", "count=32" ])

	def patternize_device(self, device, exclude_bytes = 0, seed = 0):
		pattern_size = self._getsizeof(device) - exclude_bytes
		self._log("Patternizing %s with seed %d for %d bytes (%.1f MiB)" % (device, seed, pattern_size, pattern_size / 1024 / 1024))
		assert(pattern_size > 0)
		proc = subprocess.Popen([ self._patternizer_bin, str(pattern_size), str(seed) ], stdout = subprocess.PIPE)
		datahash = hashlib.md5()
		outfile = open(device, "wb")
		while True:
			data = proc.stdout.read(1024 * 1024)
			datahash.update(data)
			outfile.write(data)
			if len(data) == 0:
				break
		outfile.close()
		datahash = datahash.hexdigest()
		self._log("Patternized %s (excluded %d): %s" % (device, exclude_bytes, datahash))
		return datahash

	def patternize_rawdev(self, exclude_bytes = 0, seed = 0):
		return self.patternize_device(self._destroy_dev, exclude_bytes = exclude_bytes, seed = seed)

	def _execute_sync(self, cmd, **kwargs):
		success_codes = kwargs.get("success_codes", [ 0 ])
		cmd_str = " ".join(cmd)
		logfile = self._get_log_file(cmd_str)
		proc = subprocess.Popen(cmd, stdout = logfile, stderr = logfile)
		if "abort" in kwargs:
			time.sleep(kwargs["abort"])
			os.kill(proc.pid, signal.SIGHUP)
		proc.wait()
		logfile.flush()
		print("=" * 120, file = logfile)
		print("Process returned with returncode %d" % (proc.returncode), file = logfile)
		returncode = proc.returncode
		if proc.returncode not in success_codes:
			failmsg = "Execution of %s failed with return code %d (success would be %s)." % (cmd_str, proc.returncode, "/".join(str(x) for x in sorted(success_codes) ))
			self._log(failmsg)
			raise Exception(failmsg)
		return returncode

	def cleanup_files(self):
		self._log("Cleanup all files")
		for filename in [ _DEFAULTS["hdrbackup_file"], _DEFAULTS["key_file"], _DEFAULTS["resume_file"] ]:
			try:
				os.unlink(filename)
			except FileNotFoundError:
				pass

	def luksify(self, **kwargs):
		self._log("Luksification (parameters: %s)" % (str(kwargs)))
		cmd = [ self._luksipc_bin ]
		cmd += [ "-d", self._destroy_dev ]
		cmd += [ "-l", "4", ]
		cmd += [ "--i-know-what-im-doing" ]
		cmd += [ "--keyfile", _DEFAULTS["key_file"] ]
		cmd += [ "--backupfile", _DEFAULTS["hdrbackup_file"] ]
		cmd += [ "--resume-file", _DEFAULTS["resume_file"] ]
		if "resume" in kwargs:
			cmd += [ "--resume" ]
		if "unlockedcontainer" in kwargs:
			cmd += [ "--readdev", kwargs["unlockedcontainer"].unlockedblkdev ]
		cmd += self._additional_params
		if "additional_params" in kwargs:
			cmd += kwargs["additional_params"]
		if "success_codes" in kwargs:
			success_codes = kwargs["success_codes"]
		else:
			if "abort" not in kwargs:
				success_codes = [ 0 ]
			else:
				success_codes = [ 0, 2 ]

		if "abort" not in kwargs:
			return self._execute_sync(cmd, success_codes = success_codes)
		else:
			return self._execute_sync(cmd, abort = kwargs["abort"], success_codes = success_codes)

	def luksOpen(self):
		dmname = self._randstr(8)
		cmd = [ "cryptsetup", "luksOpen", self._destroy_dev, dmname, "-d", _DEFAULTS["key_file"] ]
		self._execute_sync(cmd)
		return self._OpenLUKSContainer(rawdatablkdev = self._destroy_dev, dmname = dmname, keyfile = _DEFAULTS["key_file"], unlockedblkdev = "/dev/mapper/" + dmname)

	def luksClose(self, openlukscontainer):
		self._execute_sync([ "cryptsetup", "luksClose", openlukscontainer.dmname ])

	def luksFormat(self, params = None):
		if params is None:
			params = [ ]
		open(_DEFAULTS["key_file"], "w").write(self._randstr(32))
		self._execute_sync([ "cryptsetup", "luksFormat", "-q", "--key-file", _DEFAULTS["key_file"] ] + params + [ self._destroy_dev ])

	def new_testcase(self, tcname):
		self._log(("=" * 60) + " " + tcname + " " + ("=" * 60))

	def finished_testcase(self, tcname, verdict):
		self._log(("=" * 60) + " " + tcname + " " + verdict + " " + ("=" * 60))

	def setup_loopdev(self, ldsize):
		assert(self._destroy_dev.startswith("/dev/loop"))
		self._log("Resetting loop device %s to %d bytes (%.1f MiB = %d kiB + %d)" % (self._destroy_dev, ldsize, ldsize / 1024 / 1024, ldsize // 1024, ldsize % 1024))
		ldbase = "/dev/shm/loopy"
		fullmegs = (ldsize + (1024 * 1024) - 1) // (1024 * 1024)
		self._execute_sync([ "losetup", "-d", self._destroy_dev ], success_codes = [ 0, 1 ])
		self._execute_sync([ "dd", "if=/dev/zero", "of=%s" % (ldbase), "bs=1M", "count=%d" % (fullmegs) ])

		# Then truncate loopy file
		f = open(ldbase, "r+")
		f.truncate(ldsize)
		f.close()

		# Then attach loop device again
		self._execute_sync([ "losetup", self._destroy_dev, ldbase ])

		self._rawdevsize = self._getsizeof(self._destroy_dev)

if __name__ == "__main__":
	print("Please check code first!")
	sys.exit(1)
#	teng = TestEngine(destroy_data_dev = "/dev/loop0", luksipc_binary = "../luksipc", logdir = "logs/")
#	teng.setup_loopdev(543 * 1024 * 1024)
	teng = TestEngine(destroy_data_dev = "/dev/sdh1", luksipc_binary = "../luksipc", logdir = "logs/")

	expected_lukshdr_len = 4096 * 512

	conversion_parameters = {
		"filltype":		("prng_rnd", ),
#		"filltype":		("prng_constant", 123456),
#		"filltype":		("zero", ),
	}

	for iteration in range(10):
		teng.new_testcase("basic")
		teng.cleanup_files()
		teng.scrub_device_hdr()

		if conversion_parameters["filltype"][0] == "prng_rnd":
			seed = random.randint(0, 0xffffffff)
			plain_datahash = teng.patternize_rawdev(expected_lukshdr_len, seed)
		elif conversion_parameters["filltype"][0] == "prng_constant":
			seed = conversion_parameters["filltype"][1]
			plain_datahash = teng.patternize_rawdev(expected_lukshdr_len, seed)
		else:
			teng.scrub_device()
			plain_datahash = teng.hash_rawdev(expected_lukshdr_len)
		header_hash = teng.hash_rawdev(total_size = 128 * 1024 * 1024)

		# Luksify with abort
		returncode = teng.luksify(abort = 15)
		teng.verify_file(_DEFAULTS["hdrbackup_file"], header_hash)
		while returncode == 2:
			# Resume luksification
			returncode = teng.luksify(resume = _DEFAULTS["resume_file"], abort = random.randint(20, 50))
			teng.verify_file(_DEFAULTS["hdrbackup_file"], header_hash)

		# Verify initial luksification worked
		container = teng.luksOpen()
		try:
			teng.verify_device(container.unlockedblkdev, plain_datahash)
		finally:
			teng.luksClose(container)

		# Reluksify
		header_hash = teng.hash_rawdev(total_size = 128 * 1024 * 1024)
		container = teng.luksOpen()
		try:
			# Now reluksify
			teng.cleanup_files()
			returncode = teng.luksify(unlockedcontainer = container, abort = 15)
			teng.verify_file(_DEFAULTS["hdrbackup_file"], header_hash)
			while returncode == 2:
				# Resume reluksification
				returncode = teng.luksify(unlockedcontainer = container, resume = _DEFAULTS["resume_file"], abort = random.randint(20, 50))
				teng.verify_file(_DEFAULTS["hdrbackup_file"], header_hash)
		finally:
			teng.luksClose(container)

		# And verify again
		container = teng.luksOpen()
		try:
			teng.verify_device(container.unlockedblkdev, plain_datahash)
		finally:
			teng.luksClose(container)


