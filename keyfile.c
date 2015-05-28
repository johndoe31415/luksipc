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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "logging.h"
#include "keyfile.h"
#include "utils.h"
#include "random.h"

bool genKeyfile(const char *aFilename, bool aForce) {
	/* Does the file already exist? */
	struct stat statBuf;
	int statResult = stat(aFilename, &statBuf);
	if (statResult == 0) {
		/* Keyfile already exists */
		if (!aForce) {
			logmsg(LLVL_ERROR, "Keyfile %s already exists, refusing to overwrite.\n", aFilename);
			return false;
		} else {
			logmsg(LLVL_WARN, "Keyfile %s already exists, overwriting because safety checks have been disabled.\n", aFilename);
		}
	}

	int fd = open(aFilename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		/* Cannot create keyfile */
		logmsg(LLVL_ERROR, "Cannot create keyfile %s: %s\n", aFilename, strerror(errno));
		return false;
	}

	uint8_t keyData[4096];
	if (!readRandomData(keyData, sizeof(keyData))) {
		logmsg(LLVL_ERROR, "Error reading random data.\n");
		close(fd);
		return false;
	}

	int dataWritten = write(fd, keyData, sizeof(keyData));
	if (dataWritten != sizeof(keyData)) {
		logmsg(LLVL_ERROR, "Short write to keyfile: wanted %ld, read %d bytes\n", sizeof(keyData), dataWritten);
		close(fd);
		return false;
	}

	close(fd);
	return true;
}
