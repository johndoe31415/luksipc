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

bool genKeyfile(const char *aFilename) {
	/* Does the file already exist? */
	struct stat statBuf;
	int statResult, fd, rnd;
	statResult = stat(aFilename, &statBuf);
	if ((statResult != -1) || (errno != ENOENT)) {
		/* Either keyfile already exists or directory inaccessible */
		logmsg(LLVL_ERROR, "Keyfile %s either already exists (refuse to overwrite) or directory is inaccessible (cannot create).\n", aFilename);
		return false;
	}

	fd = open(aFilename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		/* Cannot create keyfile */
		logmsg(LLVL_ERROR, "Cannot create keyfile %s: %s\n", aFilename, strerror(errno));
		return false;
	}

	rnd = open("/dev/urandom", O_RDONLY);
	if (rnd == -1) {
		/* Cannot open random file */
		logmsg(LLVL_ERROR, "Cannot open random file to generate key: %s\n", strerror(errno));
		close(fd);
		return false;
	}

	{
		unsigned char buf[4096];
		int dataRead, dataWritten;
		dataRead = read(rnd, buf, sizeof(buf));
		if (dataRead != sizeof(buf)) {
			logmsg(LLVL_ERROR, "Short read from random device: wanted %ld, read %d bytes\n", sizeof(buf), dataRead);
			close(rnd);
			close(fd);
			return false;
		}
		dataWritten = write(fd, buf, sizeof(buf));
		if (dataWritten != sizeof(buf)) {
			logmsg(LLVL_ERROR, "Short write to keyfile: wanted %ld, read %d bytes\n", sizeof(buf), dataWritten);
			close(rnd);
			close(fd);
			return false;
		}
	}

	close(rnd);
	close(fd);
	return true;
}
