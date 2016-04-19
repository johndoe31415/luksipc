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

#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#define LUKSIPC_VERSION					"0.04+"

#define MAX_HANDLE_LENGTH				32

#define MAX_ARG_CNT						32
#define MAX_ARGLENGTH					256

#define EXEC_MAX_ARGCNT					64

#define RESUME_FILE_HEADER_MAGIC		"luksipc RESUME v1\0\xde\xad\xbe\xef & \xc0\xff\xee\0\0\0\0"
#define RESUME_FILE_HEADER_MAGIC_LEN	32

#define HEADER_BACKUP_BLOCKSIZE			(128 * 1024)
#define HEADER_BACKUP_BLOCKCNT			1024
#define HEADER_BACKUP_SIZE_BYTES		(HEADER_BACKUP_BLOCKSIZE * HEADER_BACKUP_BLOCKCNT)

#define DEFAULT_RESUME_FILENAME			"resume.bin"

#endif
