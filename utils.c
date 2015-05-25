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
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"
#include "logging.h"

bool safestrcpy(char *aDest, const char *aSrc, size_t aDestArraySize) {
	bool success = true;
	size_t srcLen = strlen(aSrc);
	if ((srcLen + 1) > aDestArraySize) {
		/* String does not fit, copy best effort */
		memcpy(aDest, aSrc, aDestArraySize - 1);
		success = false;
	} else {
		/* String does fit, simply copy */
		strcpy(aDest, aSrc);
	}
	return success;
}

uint64_t getDiskSizeOfFd(int aFd) {
	uint64_t result;
	if (ioctl(aFd, BLKGETSIZE64, &result) == -1) {
		perror("ioctl BLKGETSIZE64");
		result = 0;
	}
	return result;
}

uint64_t getDiskSizeOfPath(const char *aPath) {
	uint64_t diskSize;
	int fd = open(aPath, O_RDONLY);
	if (fd == -1) {
		perror("open getDiskSizeOfPath");
		diskSize = 0;
	}
	diskSize = getDiskSizeOfFd(fd);
	close(fd);
	return diskSize;
}

double getTime(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (1e-6 * tv.tv_usec);
}

bool readRandomData(uint8_t *aData, uint32_t aLength) {
	const char *randomDevice = "/dev/urandom";
	FILE *f = fopen(randomDevice, "rb");
	if (!f) {
		logmsg(LLVL_ERROR, "Error opening %s for reading entropy: %s\n", randomDevice, strerror(errno));
		return false;
	}
	
	if (fread(aData, aLength, 1, f) != 1) {
		logmsg(LLVL_ERROR, "Short read from %s for reading entropy: %s\n", randomDevice, strerror(errno));
		fclose(f);
		return false;
	}
	
	fclose(f);
	return true;
}

bool randomHexStrCat(char *aString, int aByteLen) {
	/* Generate hex data */
	uint8_t rnd[aByteLen];
	if (!readRandomData(rnd, aByteLen)) {
		logmsg(LLVL_ERROR, "Cannot generate randomized hex tag.\n");
		return false;
	}

	/* Walk string until the end */
	aString = aString + strlen(aString);

	/* Then append hex data there */
	for (int i = 0; i < aByteLen; i++) {
		sprintf(aString, "%02x", rnd[i]);
		aString += 2;
	}
	return true;
}

bool doesFileExist(const char *aFilename) {
	struct stat statBuf;
	int statResult = stat(aFilename, &statBuf);
	return statResult == 0;
}
