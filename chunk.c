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
#include "random.h"

bool allocChunk(struct chunk *aChunk, uint32_t aSize) {
	memset(aChunk, 0, sizeof(struct chunk));
	aChunk->size = aSize;
	aChunk->data = malloc(aSize);
	if (!aChunk->data) {
		return false;
	}
	memset(aChunk->data, 0, aSize);
	return true;
}

void freeChunk(struct chunk *aChunk) {
	free(aChunk->data);
	memset(aChunk, 0, sizeof(struct chunk));
}

static bool checkedSeek(int aFd, off_t aOffset, const char *aCaller) {
	off_t curOffset = lseek(aFd, aOffset, SEEK_SET);
	if (curOffset != aOffset) {
		logmsg(LLVL_WARN, "%s: tried seek to 0x%lx, went to 0x%lx (%s)\n", aCaller, aOffset, curOffset, strerror(errno));
		return false;
	}
	return true;
}

ssize_t chunkReadAt(struct chunk *aChunk, int aFd, uint64_t aOffset, uint32_t aSize) {
	ssize_t bytesRead;
	if (!checkedSeek(aFd, aOffset, "chunkReadAt")) {
		return -1;
	}
	if (aSize > aChunk->size) {
		logmsg(LLVL_CRITICAL, "chunkReadAt: Refusing to read %u bytes with only a %u bytes large buffer.\n", aSize, aChunk->size);
		return -1;
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
	if (!checkedSeek(aFd, aOffset, "chunkWriteAt")) {
		return -1;
	}
	bytesWritten = write(aFd, aChunk->data, aChunk->used);
	if (bytesWritten != aChunk->used) {
		logmsg(LLVL_WARN, "Requested write of %d bytes unsuccessful (wrote %ld).\n", aChunk->used, bytesWritten);
	}
	return bytesWritten;
}

#ifdef DEVELOPMENT
/* Don't even compile these variants in if we're not in a development build so
 * there's no possibility they get used accidently */

ssize_t unreliableChunkReadAt(struct chunk *aChunk, int aFd, uint64_t aOffset, uint32_t aSize) {
	if (randomEvent(100)) {
		logmsg(LLVL_WARN, "Fault injection: Failing unreliable read at offset 0x%lx.\n", aOffset);
		return -1;
	} else {
		return chunkReadAt(aChunk, aFd, aOffset, aSize);
	}
}

ssize_t unreliableChunkWriteAt(struct chunk *aChunk, int aFd, uint64_t aOffset) {
	if (randomEvent(100)) {
		logmsg(LLVL_WARN, "Fault injection: Failing unreliable write at offset 0x%lx.\n", aOffset);
		return -1;
	} else {
		return chunkWriteAt(aChunk, aFd, aOffset);
	}
}
#endif
