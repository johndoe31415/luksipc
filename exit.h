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

#ifndef __EXIT_H__
#define __EXIT_H__

/*
 * The error codes and messages are maintained here. All C code is generated
 * from these stubs from a small Python script. When adding new error codes,
 * please add them here and regenerate the appropriate code. The format should
 * be fairly obvious.
 *
:0	EC_SUCCESS												Success
:1	EC_UNSPECIFIED_ERROR									Unspecified error
:2	EC_COPY_ABORTED_RESUME_FILE_WRITTEN						Copy aborted gracefully, resume file successfully written
:3	EC_CANNOT_ALLOCATE_CHUNK_MEMORY							Cannot allocate memory for copy chunks
:4	EC_CANNOT_GENERATE_KEY_FILE								Cannot generate key file
:5	EC_CANNOT_INITIALIZE_DEVICE_ALIAS						Cannot initialize device mapper alias
:6	EC_CANNOT_OPEN_READ_DEVICE								Cannot open reading block device
:7	EC_CANNOT_OPEN_RESUME_FILE								Cannot open resume file
:8	EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE		Copy aborted, failed to write resume file
:9	EC_DEVICE_SIZES_IMPLAUSIBLE								Device sizes are implausible
:10	EC_FAILED_TO_BACKUP_HEADER								Failed to backup raw device header
:11	EC_FAILED_TO_CLOSE_LUKS_DEVICE							Failed to close LUKS device
:12	EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE				Failed to open unlocked crypto device
:13	EC_FAILED_TO_PERFORM_LUKSFORMAT							Failed to perform luksFormat
:14	EC_FAILED_TO_PERFORM_LUKSOPEN							Failed to perform luksOpen
:15	EC_FAILED_TO_READ_RESUME_FILE							Failed to read resume file
:16	EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS					Failed to remove device mapper alias
:17	EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE				Device mapper handle for luksipc write device is unavailable
:18	EC_PRECONDITIONS_NOT_SATISFIED							Process preconditions are unsatisfied
:19	EC_UNABLE_TO_GET_RAW_DISK_SIZE							Unable to determine raw disk size
:20	EC_UNABLE_TO_READ_FIRST_CHUNK							Unable to read first chunk
:21	EC_UNABLE_TO_READ_FROM_STDIN							Unable to read from standard input
:22	EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE					Unsupported small disk corner case
:23	EC_USER_ABORTED_PROCESS									User aborted process
:24	EC_CANNOT_INIT_SIGNAL_HANDLERS							Unable to install signal handlers
:25	EC_CMDLINE_PARSING_ERROR								Error parsing the parameters given on command line (programming bug)
:26	EC_CMDLINE_ARGUMENT_ERROR								Error with a parameter which was given on the command line
:27	EC_CANNOT_GENERATE_WRITE_HANDLE							Error generating device mapper write handle
:28	EC_PRNG_INITIALIZATION_FAILED							Initialization of PRNG failed
*/

enum terminationCode_t {
	EC_SUCCESS = 0,
	EC_UNSPECIFIED_ERROR = 1,
	EC_COPY_ABORTED_RESUME_FILE_WRITTEN = 2,
	EC_CANNOT_ALLOCATE_CHUNK_MEMORY = 3,
	EC_CANNOT_GENERATE_KEY_FILE = 4,
	EC_CANNOT_INITIALIZE_DEVICE_ALIAS = 5,
	EC_CANNOT_OPEN_READ_DEVICE = 6,
	EC_CANNOT_OPEN_RESUME_FILE = 7,
	EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE = 8,
	EC_DEVICE_SIZES_IMPLAUSIBLE = 9,
	EC_FAILED_TO_BACKUP_HEADER = 10,
	EC_FAILED_TO_CLOSE_LUKS_DEVICE = 11,
	EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE = 12,
	EC_FAILED_TO_PERFORM_LUKSFORMAT = 13,
	EC_FAILED_TO_PERFORM_LUKSOPEN = 14,
	EC_FAILED_TO_READ_RESUME_FILE = 15,
	EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS = 16,
	EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE = 17,
	EC_PRECONDITIONS_NOT_SATISFIED = 18,
	EC_UNABLE_TO_GET_RAW_DISK_SIZE = 19,
	EC_UNABLE_TO_READ_FIRST_CHUNK = 20,
	EC_UNABLE_TO_READ_FROM_STDIN = 21,
	EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE = 22,
	EC_USER_ABORTED_PROCESS = 23,
	EC_CANNOT_INIT_SIGNAL_HANDLERS = 24,
	EC_CMDLINE_PARSING_ERROR = 25,
	EC_CMDLINE_ARGUMENT_ERROR = 26,
	EC_CANNOT_GENERATE_WRITE_HANDLE = 27,
	EC_PRNG_INITIALIZATION_FAILED = 28
};

/*************** AUTO GENERATED SECTION FOLLOWS ***************/
void terminate(enum terminationCode_t aTermCode);
/***************  AUTO GENERATED SECTION ENDS   ***************/

#endif
