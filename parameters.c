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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include "logging.h"
#include "parameters.h"
#include "globals.h"

static void defaultParameters(struct parameters *aParams) {
	memset(aParams, 0, sizeof(struct parameters));
	aParams->blocksize = 10 * 1024 * 1024;
	aParams->batchMode = false;
	aParams->keyFile = "/root/initial_keyfile.bin";
	aParams->logLevel = LLVL_INFO;
}

static void syntax(char **argv, const char *aMessage) {
	if (aMessage) {
		fprintf(stderr, "Error: %s\n", aMessage);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "luksipc: Tool to convert block devices to LUKS-encrypted block devices on the fly");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s (--device=DEVPATH) (--blocksize=BYTES) (--loglevel=LLVL)\n", argv[0]);
	fprintf(stderr, "    (--i-know-what-im-doing) (--resume=FILE) (--keyfile=FILE) (--luksparam=PARAMS)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    --device=DEVPATH       Device that is about to be converted to LUKS. Mandatory argument.\n");
	fprintf(stderr, "    -d DEVPATH             \n");
	fprintf(stderr, "    --blocksize=BYTES      Specify block size for copying in bytes. Default (and minimum) size is\n");
	fprintf(stderr, "    -b BYTES               10 MiB (10485760 bytes). This value is rounded up to closest 4096-byte\n");
	fprintf(stderr, "                           value automatically. It must be at least size of LUKS header (usually\n");
	fprintf(stderr, "                           2048 kiB, but may vary).\n");
	fprintf(stderr, "    --loglevel=LLVL        Integer value that specifies the level of logging verbosity from 0 to\n");
	fprintf(stderr, "    -l LLVL                4 (critical, error, warn, info, debug). Default loglevel is 3 (info).\n");
	fprintf(stderr, "    --i-know-what-im-doing Enable batch mode (will not ask any questions or confirmations\n");
	fprintf(stderr, "                           interactively).\n");
	fprintf(stderr, "    --resume=FILE          Resume a interrupted conversion with the help of a resume file. This\n");
	fprintf(stderr, "                           file is generated when luksipc aborts, is usually called resume.bin\n");
	fprintf(stderr, "                           and is located in the directory from which you ran luksipc the first\n");
	fprintf(stderr, "                           time.\n");
	fprintf(stderr, "    --keyfile=FILE         Filename for the initial keyfile. A 4096 bytes long file will be\n");
	fprintf(stderr, "    -k FILE                generated under this location which has /dev/urandom as the input. It\n");
	fprintf(stderr, "                           will be added as the first keyslot in the luksFormat process. If you\n");
	fprintf(stderr, "                           put this file on a volatile device such as /dev/shm, remember that all\n");
	fprintf(stderr, "                           your data is garbage after a reboot if you forget to add a second key\n");
	fprintf(stderr, "                           to the LUKS keyring. The default filename is\n");
	fprintf(stderr, "                           /root/initial_keyfile.bin. This file will always be created with 0o600\n");
	fprintf(stderr, "                           permissions.\n");
	fprintf(stderr, "    --luksparam=PARAMS     Pass these additional options to luksFormat, for example to select a\n");
	fprintf(stderr, "    -p PARAMS              different cipher. Parameters have to be passed comma-separated.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "    %s -d /dev/sda9\n", argv[0]);
	fprintf(stderr, "       Converts /dev/sda9 to a LUKS partition with default parameters.\n");
	fprintf(stderr, "    %s -d /dev/sda9 -k /root/secure_key/keyfile.bin --luksparams='-c,twofish-lrw-benbi,-s,320,-h,sha256'\n", argv[0]);
	fprintf(stderr, "       Converts /dev/sda9 to a LUKS partition and stores the initially used keyfile in\n");
	fprintf(stderr, "       /root/secure_key/keyfile.bin. Additionally some LUKS parameters are passed that specify that the\n");
	fprintf(stderr, "       Twofish cipher should be used with a 320 bit keysize and SHA-256 as a hash function.\n");
	fprintf(stderr, "    %s -d /dev/sda9 --resume /root/resume.bin\n", argv[0]);
	fprintf(stderr, "       Resumes a crashed LUKS conversion of /dev/sda9 using the file /root/resume.bin which was\n");
	fprintf(stderr, "       generated at the first (crashed) luksipc run.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "luksipc version: " LUKSIPC_VERSION "\n");
	exit(EXIT_FAILURE);
}

static void checkParameters(char **argv, const struct parameters *aParams) {
	char errorMessage[256];
	if (!aParams->device) {
		syntax(argv, "No device to convert was given on the command line");
	}
	if (aParams->blocksize < MINBLOCKSIZE) {
		snprintf(errorMessage, sizeof(errorMessage), "Blocksize needs to be at the very least %d bytes (size of LUKS header), user specified %d bytes.", MINBLOCKSIZE, aParams->blocksize);
		syntax(argv, errorMessage);
	}
	if ((aParams->logLevel < 0) || (aParams->logLevel > LLVL_DEBUG)) {
		snprintf(errorMessage, sizeof(errorMessage), "Loglevel needs to be inbetween 0 and %d, user specified %d.", LLVL_DEBUG, aParams->logLevel);
		syntax(argv, errorMessage);
	}
}

void parseParameters(struct parameters *aParams, int argc, char **argv) {
	struct option longOptions[] = {
		{ "blocksize", 1, NULL, 'b' },
		{ "device", 1, NULL, 'd' },
		{ "i-know-what-im-doing", 0, NULL, 'i' },
		{ "i-know-what-im-doinx", 0, NULL, '?' },		/* Do not allow abbreviation of that one */
		{ "loglevel", 1, NULL, 'l' },
		{ "resume", 0, NULL, 'r' },
		{ "keyfile", 1, NULL, 'k' },
		{ "luksparams", 1, NULL, 'p' },
		{ 0, 0, 0, 0 }
	};
	int character;

	defaultParameters(aParams);
	while ((character = getopt_long(argc, argv, "b:d:l:k:p:", longOptions, NULL)) != -1) {
		switch (character) {
			case '?':
				fprintf(stderr, "\n");
				syntax(argv, NULL);
				break;

			case 'b':
				aParams->blocksize = atoi(optarg);
				break;

			case 'd':
				aParams->device = optarg;
				break;

			case 'i':
				aParams->batchMode = true;
				break;

			case 'l': {
				char *endPtr = NULL;
				aParams->logLevel = strtol(optarg, &endPtr, 10);
				if ((endPtr == NULL) || (*endPtr != 0)) {
					fprintf(stderr, "Error: Cannot convert the value '%s' you passed as a log level (must be an integer).\n", optarg);
					exit(EXIT_FAILURE);
				}
				if ((aParams->logLevel < 0) || (aParams->logLevel > 4)) {
					fprintf(stderr, "Error: Log level must be between 0 and 4.\n");
					exit(EXIT_FAILURE);
				}
				break;
			}

			case 'r':
				aParams->resume = true;
				break;

			case 'k':
				aParams->keyFile = optarg;
				break;

			case 'p':
				aParams->luksFormatParams = optarg;
				break;

			default:
				fprintf(stderr, "Error: Lazy programmer caused bug in getopt parsing.\n");
				exit(EXIT_FAILURE);
		}
	}

	/* Round up block size to 4096 bytes multiple */
	aParams->blocksize = ((aParams->blocksize + 4095) / 4096) * 4096;

	checkParameters(argv, aParams);

	setLogLevel(aParams->logLevel);
}

