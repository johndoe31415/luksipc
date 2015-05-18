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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include "logging.h"
#include "chunk.h"

void allocChunk(struct chunk *aChunk, uint32_t aSize) {
	memset(aChunk, 0, sizeof(struct chunk));
	aChunk->size = aSize;
	aChunk->data = malloc(aSize);
	if (!aChunk->data) {
		perror("malloc chunk");
		exit(EXIT_FAILURE);
	}
	memset(aChunk->data, 0, aSize);
}

void freeChunk(struct chunk *aChunk) {
	free(aChunk->data);
	memset(aChunk, 0, sizeof(struct chunk));
}

static void safeSeek(int aFd, off64_t aOffset, const char *aCaller) {
	off64_t curOffset = lseek64(aFd, aOffset, SEEK_SET);
	if (curOffset != aOffset) {
		logmsg(LLVL_WARN, "%s: tried seek to 0x%lx, went to 0x%lx (%s)\n", aCaller, aOffset, curOffset, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

ssize_t chunkReadAt(struct chunk *aChunk, int aFd, uint64_t aOffset, uint32_t aSize) {
	ssize_t bytesRead;
	safeSeek(aFd, aOffset, "chunkReadAt");
	if (aSize > aChunk->size) {
		logmsg(LLVL_CRITICAL, "chunkReadAt: Refusing to read %d bytes with a %d bytes buffer.\n", aSize, aChunk->size);
		exit(EXIT_FAILURE);
	}
	bytesRead = read(aFd, aChunk->data, aSize);
	if (bytesRead < 0) {
		aChunk->used = 0;
	} else {
		aChunk->used = bytesRead;
	}
	return bytesRead;
}

ssize_t chunkWriteAt(const struct chunk *aChunk, int aFd, uint64_t aOffset) {
	ssize_t bytesWritten;
	safeSeek(aFd, aOffset, "chunkWriteAt");
	bytesWritten = write(aFd, aChunk->data, aChunk->used);
	if (bytesWritten != aChunk->used) {
		logmsg(LLVL_WARN, "Requested write of %d bytes unsuccessful (wrote %ld).\n", aChunk->used, bytesWritten);
	}
	return bytesWritten;
}
