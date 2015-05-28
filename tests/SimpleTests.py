import random

from TestEngine import LUKSIPCTest

class SimpleLUKSIPCTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_device()
		self._assert(self._engine.luksify() == 0, "LUKSification failed")
		self.verify_container(params)


class AbortedLUKSIPCTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_device()

		returncode = self._engine.luksify(abort = 20)
		self._engine.verify_hdrbackup_file(params.backup_header_hash)		
		while returncode == 2:
			returncode = self._engine.luksify(abort = random.randint(10, 60), resume = True)
			self._engine.verify_hdrbackup_file(params.backup_header_hash)		

		self.verify_container(params)
	

class IOErrorLUKSIPCTest(LUKSIPCTest):
	def run(self):
		params = self.prepare_device()

		returncode = self._engine.luksify(additional_params = [ "--development-ioerrors" ], success_codes = [ 0, 2 ])
		self._engine.verify_hdrbackup_file(params.backup_header_hash)		
		while returncode == 2:
			returncode = self._engine.luksify(resume = True, additional_params = [ "--development-ioerrors" ], success_codes = [ 0, 2 ])
			self._engine.verify_hdrbackup_file(params.backup_header_hash)		

		self.verify_container(params)
