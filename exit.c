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

#include "logging.h"
#include "exit.h"

#define MAX_VALID_ERROR_CODE		28
static const char *exitCodeAbbr[] = {
	[EC_SUCCESS] = "EC_SUCCESS",
	[EC_UNSPECIFIED_ERROR] = "EC_UNSPECIFIED_ERROR",
	[EC_COPY_ABORTED_RESUME_FILE_WRITTEN] = "EC_COPY_ABORTED_RESUME_FILE_WRITTEN",
	[EC_CANNOT_ALLOCATE_CHUNK_MEMORY] = "EC_CANNOT_ALLOCATE_CHUNK_MEMORY",
	[EC_CANNOT_GENERATE_KEY_FILE] = "EC_CANNOT_GENERATE_KEY_FILE",
	[EC_CANNOT_INITIALIZE_DEVICE_ALIAS] = "EC_CANNOT_INITIALIZE_DEVICE_ALIAS",
	[EC_CANNOT_OPEN_READ_DEVICE] = "EC_CANNOT_OPEN_READ_DEVICE",
	[EC_CANNOT_OPEN_RESUME_FILE] = "EC_CANNOT_OPEN_RESUME_FILE",
	[EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE] = "EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE",
	[EC_DEVICE_SIZES_IMPLAUSIBLE] = "EC_DEVICE_SIZES_IMPLAUSIBLE",
	[EC_FAILED_TO_BACKUP_HEADER] = "EC_FAILED_TO_BACKUP_HEADER",
	[EC_FAILED_TO_CLOSE_LUKS_DEVICE] = "EC_FAILED_TO_CLOSE_LUKS_DEVICE",
	[EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE] = "EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE",
	[EC_FAILED_TO_PERFORM_LUKSFORMAT] = "EC_FAILED_TO_PERFORM_LUKSFORMAT",
	[EC_FAILED_TO_PERFORM_LUKSOPEN] = "EC_FAILED_TO_PERFORM_LUKSOPEN",
	[EC_FAILED_TO_READ_RESUME_FILE] = "EC_FAILED_TO_READ_RESUME_FILE",
	[EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS] = "EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS",
	[EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE] = "EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE",
	[EC_PRECONDITIONS_NOT_SATISFIED] = "EC_PRECONDITIONS_NOT_SATISFIED",
	[EC_UNABLE_TO_GET_RAW_DISK_SIZE] = "EC_UNABLE_TO_GET_RAW_DISK_SIZE",
	[EC_UNABLE_TO_READ_FIRST_CHUNK] = "EC_UNABLE_TO_READ_FIRST_CHUNK",
	[EC_UNABLE_TO_READ_FROM_STDIN] = "EC_UNABLE_TO_READ_FROM_STDIN",
	[EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE] = "EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE",
	[EC_USER_ABORTED_PROCESS] = "EC_USER_ABORTED_PROCESS",
	[EC_CANNOT_INIT_SIGNAL_HANDLERS] = "EC_CANNOT_INIT_SIGNAL_HANDLERS",
	[EC_CMDLINE_PARSING_ERROR] = "EC_CMDLINE_PARSING_ERROR",
	[EC_CMDLINE_ARGUMENT_ERROR] = "EC_CMDLINE_ARGUMENT_ERROR",
	[EC_CANNOT_GENERATE_WRITE_HANDLE] = "EC_CANNOT_GENERATE_WRITE_HANDLE",
	[EC_PRNG_INITIALIZATION_FAILED] = "EC_PRNG_INITIALIZATION_FAILED",
};
static const char *exitCodeDesc[] = {
	[EC_SUCCESS] = "Success",
	[EC_UNSPECIFIED_ERROR] = "Unspecified error",
	[EC_COPY_ABORTED_RESUME_FILE_WRITTEN] = "Copy aborted gracefully, resume file successfully written",
	[EC_CANNOT_ALLOCATE_CHUNK_MEMORY] = "Cannot allocate memory for copy chunks",
	[EC_CANNOT_GENERATE_KEY_FILE] = "Cannot generate key file",
	[EC_CANNOT_INITIALIZE_DEVICE_ALIAS] = "Cannot initialize device mapper alias",
	[EC_CANNOT_OPEN_READ_DEVICE] = "Cannot open reading block device",
	[EC_CANNOT_OPEN_RESUME_FILE] = "Cannot open resume file",
	[EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE] = "Copy aborted, failed to write resume file",
	[EC_DEVICE_SIZES_IMPLAUSIBLE] = "Device sizes are implausible",
	[EC_FAILED_TO_BACKUP_HEADER] = "Failed to backup raw device header",
	[EC_FAILED_TO_CLOSE_LUKS_DEVICE] = "Failed to close LUKS device",
	[EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE] = "Failed to open unlocked crypto device",
	[EC_FAILED_TO_PERFORM_LUKSFORMAT] = "Failed to perform luksFormat",
	[EC_FAILED_TO_PERFORM_LUKSOPEN] = "Failed to perform luksOpen",
	[EC_FAILED_TO_READ_RESUME_FILE] = "Failed to read resume file",
	[EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS] = "Failed to remove device mapper alias",
	[EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE] = "Device mapper handle for luksipc write device is unavailable",
	[EC_PRECONDITIONS_NOT_SATISFIED] = "Process preconditions are unsatisfied",
	[EC_UNABLE_TO_GET_RAW_DISK_SIZE] = "Unable to determine raw disk size",
	[EC_UNABLE_TO_READ_FIRST_CHUNK] = "Unable to read first chunk",
	[EC_UNABLE_TO_READ_FROM_STDIN] = "Unable to read from standard input",
	[EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE] = "Unsupported small disk corner case",
	[EC_USER_ABORTED_PROCESS] = "User aborted process",
	[EC_CANNOT_INIT_SIGNAL_HANDLERS] = "Unable to install signal handlers",
	[EC_CMDLINE_PARSING_ERROR] = "Error parsing the parameters given on command line (programming bug)",
	[EC_CMDLINE_ARGUMENT_ERROR] = "Error with a parameter which was given on the command line",
	[EC_CANNOT_GENERATE_WRITE_HANDLE] = "Error generating device mapper write handle",
	[EC_PRNG_INITIALIZATION_FAILED] = "Initialization of PRNG failed",
};

void terminate(enum terminationCode_t aTermCode) {
	int logLevel = (aTermCode == EC_SUCCESS) ? LLVL_DEBUG : LLVL_ERROR;
	if (aTermCode <= MAX_VALID_ERROR_CODE) {
		logmsg(logLevel, "Exit with code %d [%s]: %s\n", aTermCode, exitCodeAbbr[aTermCode], exitCodeDesc[aTermCode]);
	} else {
		logmsg(LLVL_ERROR, "Exit with code %d: No description available.\n", aTermCode);
	}
	exit(aTermCode);
}
