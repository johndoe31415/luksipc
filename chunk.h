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

#ifndef __CHUNK_H__
#define __CHUNK_H__

#include <stdint.h>
#include <stdbool.h>

struct chunk {
	uint32_t size;			/* Total chunk size */
	uint32_t used;			/* Used chunk size */
	uint8_t *data;			/* Data */
};

/*************** AUTO GENERATED SECTION FOLLOWS ***************/
bool allocChunk(struct chunk *aChunk, uint32_t aSize);
void freeChunk(struct chunk *aChunk);
ssize_t chunkReadAt(struct chunk *aChunk, int aFd, uint64_t aOffset, uint32_t aSize);
ssize_t chunkWriteAt(const struct chunk *aChunk, int aFd, uint64_t aOffset);
ssize_t unreliableChunkReadAt(struct chunk *aChunk, int aFd, uint64_t aOffset, uint32_t aSize);
ssize_t unreliableChunkWriteAt(struct chunk *aChunk, int aFd, uint64_t aOffset);
/***************  AUTO GENERATED SECTION ENDS   ***************/

#endif
