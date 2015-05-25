/*
	luksipc - Tool to convert block devices to LUKS in-place.
	Copyright (C) 2011-2015 Johannes Bauer

	This file is part of luksipc.

	luksipc is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; this program is ONLY licensed under
	version 3 of the License, later versions are explicitly excluded.

	luksipc is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with luksipc; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	Johannes Bauer <JohannesBauer@gmx.de>
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "exec.h"
#include "luks.h"
#include "logging.h"
#include "globals.h"
#include "utils.h"

/* Checks is the given block device has already been formatted with LUKS. */
bool isLuks(const char *aBlockDevice) {
	const char *arguments[] = {
		"cryptsetup",
		"isLuks",
		aBlockDevice,
		NULL
	};
	int returnCode = execGetReturnCode(arguments);
	return returnCode == 0;
}


/* Returns if the given device mapper name is available (i.e. not active at the
 * moment) */
bool isLuksMapperAvailable(const char *aMapperName) {
	const char *arguments[] = {
		"cryptsetup",
		"status",
		aMapperName,
		NULL
	};

	logmsg(LLVL_DEBUG, "Performing dm-crypt status lookup on mapper name '%s'\n", aMapperName);
	int returnCode = execGetReturnCode(arguments);
	bool mapperAvailable = (returnCode == 4);
	logmsg(LLVL_DEBUG, "Device mapper name '%s' is %savailable (returncode %d).\n", aMapperName, mapperAvailable ? "" : "NOT ", returnCode);
	return mapperAvailable;
}

/* Formats a block device with LUKS using the given key file for slot 0 and
 * passes some optional parameters (comma-separated) to cryptsetup */
bool luksFormat(const char *aBlkDevice, const char *aKeyFile, const char *aOptionalParams) {
	int argcnt = -1;
	char userSuppliedArguments[MAX_ARGLENGTH];
	const char *arguments[MAX_ARG_CNT] = {
		"cryptsetup",
		"luksFormat",
		"-q",
		"--key-file",
		aKeyFile,
		NULL
	};
	if (aOptionalParams) {
		if (!safestrcpy(userSuppliedArguments, aOptionalParams, MAX_ARGLENGTH)) {
			logmsg(LLVL_ERROR, "Unable to copy user supplied argument, %d bytes max.\n", MAX_ARGLENGTH);
			return false;
		}

		if (!argAppendParse(arguments, userSuppliedArguments, &argcnt, MAX_ARG_CNT)) {
			logmsg(LLVL_ERROR, "Unable to copy user supplied argument, %d count max.\n", MAX_ARG_CNT);
			return false;
		}
	}

	if (!argAppend(arguments, aBlkDevice, &argcnt, MAX_ARG_CNT)) {
		logmsg(LLVL_ERROR, "Unable to copy last user supplied argument, %d count max.\n", MAX_ARG_CNT);
		return false;
	}

	logmsg(LLVL_DEBUG, "Performing luksFormat of block device %s using key file %s\n", aBlkDevice, aKeyFile);
	int returnCode = execGetReturnCode(arguments);
	if (returnCode != 0) {
		logmsg(LLVL_ERROR, "luksFormat failed (return code %d), aborting.\n", returnCode);
		return false;
	}

	return true;
}

bool luksOpen(const char *aBlkDevice, const char *aKeyFile, const char *aHandle) {
	const char *arguments[] = {
		"cryptsetup",
		"luksOpen",
		"--key-file",
		aKeyFile,
		aBlkDevice,
		aHandle,
		NULL
	};
	logmsg(LLVL_DEBUG, "Performing luksOpen of block device %s using key file %s and device mapper handle %s\n", aBlkDevice, aKeyFile, aHandle);
	int returnCode = execGetReturnCode(arguments);
	if (returnCode != 0) {
		logmsg(LLVL_ERROR, "luksOpen failed (return code %d).\n", returnCode);
		return false;
	}

	return true;
}

bool luksClose(const char *aMapperHandle) {
	const char *arguments[] = {
		"dmsetup",
		"remove",
		aMapperHandle,
		NULL
	};

	/* Device cannot be closed if there is internal caches. These are not
	 * flushed even by sync(2). So we have to do the horrible: Retry until we
	 * succeed :-( */ 
	bool success = false;
	for (int try = 0; try < 25; try++) {
		int returnCode = execGetReturnCode(arguments);
		success = (returnCode == 0);
		if (success) {
			break;
		}
		sleep(1);
	}

	return success && isLuksMapperAvailable(aMapperHandle);
}

bool dmCreateAlias(const char *aSrcDevice, const char *aMapperHandle) {
	uint64_t devSize = getDiskSizeOfPath(aSrcDevice);
	if (devSize % 512) {
		logmsg(LLVL_ERROR, "Device size of %s (%lu bytes) is not divisible by even 512 bytes sector size.\n", aSrcDevice, devSize);
		return false;
	}

	char mapperTable[256];
	snprintf(mapperTable, sizeof(mapperTable), "0 %lu linear %s 0", devSize / 512, aSrcDevice);

	const char *arguments[] = {
		"dmsetup",
		"create",
		aMapperHandle,
		"--table",
		mapperTable,
		NULL
	};

	int returnCode = execGetReturnCode(arguments);
	if (returnCode != 0) {
		logmsg(LLVL_ERROR, "dmsetup alias creation failed (returncode %d).\n", returnCode);
		return false;
	}

	char aliasDeviceFilename[256];
	snprintf(aliasDeviceFilename, sizeof(aliasDeviceFilename), "/dev/mapper/%s", aMapperHandle);
	uint64_t aliasDevSize = getDiskSizeOfPath(aliasDeviceFilename);	
	if (devSize != aliasDevSize) {
		logmsg(LLVL_ERROR, "Source device (%s) and its supposed alias device (%s) have different sizes (src = %lu and alias = %lu).\n", aSrcDevice, aliasDeviceFilename, devSize, aliasDevSize);
		dmRemoveAlias(aMapperHandle);
		return false;
	}

	logmsg(LLVL_DEBUG, "Created device mapper alias: %s -> %s\n", aliasDeviceFilename, aSrcDevice);
	return true;
}

char *dmCreateDynamicAlias(const char *aSrcDevice, const char *aAliasPrefix) {
	char alias[64];
	if (aAliasPrefix && (strlen(aAliasPrefix) < 32)) {
		snprintf(alias, sizeof(alias), "alias_%s_", aAliasPrefix);
	} else {
		strcpy(alias, "alias_");
	}
	if (!randomHexStrCat(alias, 4)) {
		return NULL;
	}

	char *aliasPathname = malloc(strlen("/dev/mapper/") + strlen(alias) + 1);
	if (!aliasPathname) {
		logmsg(LLVL_ERROR, "malloc error for full filename of dynamic alias: %s\n", strerror(errno));
		return NULL;
	}
	sprintf(aliasPathname, "/dev/mapper/%s", alias);
	
	bool aliasSuccessful = dmCreateAlias(aSrcDevice, alias);
	if (!aliasSuccessful) {
		free(aliasPathname);
		return NULL;
	}

	return aliasPathname;	
}

bool dmRemoveAlias(const char *aMapperHandle) {
	const char *arguments[] = {
		"dmsetup",
		"remove",
		aMapperHandle,
		NULL
	};
	int returnCode = execGetReturnCode(arguments);
	if (returnCode != 0) {
		logmsg(LLVL_ERROR, "Cannot remove device mapper handle %s (return code %d)\n", aMapperHandle, returnCode);
	}
	return returnCode == 0;
}

