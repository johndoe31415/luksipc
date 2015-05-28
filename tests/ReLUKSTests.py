import random

from TestEngine import LUKSIPCTest

class SimpleReLUKSIPCBaseTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_luksdevice(self.luksformat_params)
		container = self._engine.luksOpen()
		try:
			# Now reluksify the opened container
			self._engine.cleanup_files()
			self._assert(self._engine.luksify(unlockedcontainer = container) == 0, "LUKSification failed")
		finally:
			self._engine.luksClose(container)
		self.verify_container(params)


class SimpleReLUKSIPCTest1(SimpleReLUKSIPCBaseTest):
	def run(self):
		self.luksformat_params = [ ]
		SimpleReLUKSIPCBaseTest.run(self)


class SimpleReLUKSIPCTest2(LUKSIPCTest):
	def run(self):
		self.luksformat_params = [ "-c", "twofish-lrw-benbi", "-s", "320", "-h", "sha256" ]
		SimpleReLUKSIPCBaseTest.run(self)


class AbortedReLUKSIPCTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_luksdevice()
		container = self._engine.luksOpen()
		try:
			# Now reluksify the opened container
			self._engine.cleanup_files()
			returncode = self._engine.luksify(unlockedcontainer = container, abort = 20)
			self._engine.verify_hdrbackup_file(params.backup_header_hash)		
			while returncode == 2:
				returncode = self._engine.luksify(unlockedcontainer = container, abort = random.randint(10, 60), resume = True)
				self._engine.verify_hdrbackup_file(params.backup_header_hash)		
		finally:
			self._engine.luksClose(container)
		self.verify_container(params)


class IOErrorReLUKSIPCTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_luksdevice()
		container = self._engine.luksOpen()
		try:
			# Now reluksify the opened container
			self._engine.cleanup_files()
			returncode = self._engine.luksify(unlockedcontainer = container, additional_params = [ "--development-ioerrors" ], success_codes = [ 0, 2 ])
			self._engine.verify_hdrbackup_file(params.backup_header_hash)		
			while returncode == 2:
				returncode = self._engine.luksify(unlockedcontainer = container, resume = True, additional_params = [ "--development-ioerrors" ], success_codes = [ 0, 2 ])
				self._engine.verify_hdrbackup_file(params.backup_header_hash)		
		finally:
			self._engine.luksClose(container)
		self.verify_container(params)


