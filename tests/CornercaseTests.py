import random

from TestEngine import LUKSIPCTest

class LargeHeaderLUKSIPCTest(LUKSIPCTest):
	def run(self):
		luks_header_sectors = 9999
		luks_header_bytes = luks_header_sectors * 512
		params = self.prepare_device(luks_header_bytes)
		self._assert(self._engine.luksify(additional_params = [ "--luksparams=--align-payload=%d" % (luks_header_sectors) ]) == 0, "LUKSification failed")
		self.verify_container(params)

