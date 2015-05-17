/*
	luksipc - Tool to convert block devices to LUKS in-place.
	Copyright (C) 2011-2011 Johannes Bauer
	
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

#include "logging.h"
#include "parameters.h"

static void defaultParameters(struct parameters *aParams) {
	memset(aParams, 0, sizeof(struct parameters));
	aParams->blocksize = 3 * 1024 * 1024;
	aParams->batchMode = false;
	aParams->keyFile = "/root/initial_keyfile.bin";
	aParams->logLevel = LLVL_INFO;
}

static void syntax(char **argv, const char *aMessage) {
	if (aMessage) {
		fprintf(stderr, "Error: %s\n", aMessage);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "%s (--blocksize=SIZE) (--device=DEV) (--loglevel=LVL) (--i-know-what-im-doing) (--resume=FILE)\n", argv[0]);
	fprintf(stderr, "\n");
	fprintf(stderr, "   --blocksize=SIZE   Specify block size in bytes for copying. Default is 3 MB. Rounded up to\n");
	fprintf(stderr, "   -b SIZE            closes 4096-byte value automatically. Must be at least size of LUKS header\n");
	fprintf(stderr, "                      (usually 1028 kB)\n");
	fprintf(stderr, "   --device=DEV       Device that is about to be converted to LUKS\n");
	fprintf(stderr, "   -d DEV\n");
	fprintf(stderr, "   --loglevel=LLVL    Level of logging, integer from 0 to 4 (Critical, Error, Warn, Info, Debug).\n");
	fprintf(stderr, "   -l LLVL            Default is 3 (Info)\n");
	fprintf(stderr, "   --i-know-what-im-doing\n");
	fprintf(stderr, "                      Enable batch mode (do not ask any questions)\n");
	fprintf(stderr, "   --resume=FILE      Resume a interrupted conversion with the help of a resume file.\n");
	fprintf(stderr, "   -k FILE            Filename for the initial keyfile. If you put this for example on /dev/shm,\n");
	fprintf(stderr, "   --keyfile=FILE     remember that all your data is garbage after a reboot if you didn't add a\n");
	fprintf(stderr, "                      second key to the LUKS keyring. Default is /root/initial_keyfile.bin\n");
	fprintf(stderr, "   -p PARAMS          Pass these additional options to luksForamt, for example to select a different\n");
	fprintf(stderr, "   --luksparam=PARAMS cipher. Parameters have to be passed comma-separated.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "    %s -d /dev/sda9\n", argv[0]);
	fprintf(stderr, "    %s -d /dev/sda9 -k /root/secure_key/keyfile.bin --luksparams='-c,twofish-lrw-benbi,-s,320,-h,sha256'\n", argv[0]);
	fprintf(stderr, "    %s -d /dev/sda9 --resume /root/resume.bin\n", argv[0]);
	exit(EXIT_FAILURE);
}

static void checkParameters(char **argv, const struct parameters *aParams) {
	char errorMessage[256];
	if (!aParams->device) {
		syntax(argv, "No device given");
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
			
			case 'l':
				aParams->logLevel = atoi(optarg);
				break;
			
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


