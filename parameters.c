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
#include <stdint.h>

#include "utils.h"
#include "logging.h"
#include "parameters.h"
#include "globals.h"
#include "exit.h"

static void defaultParameters(struct conversionParameters *aParams) {
	memset(aParams, 0, sizeof(struct conversionParameters));
	aParams->blocksize = 10 * 1024 * 1024;
	aParams->safetyChecks = true;
	aParams->batchMode = false;
	aParams->keyFile = "/root/initial_keyfile.bin";
	aParams->logLevel = LLVL_INFO;
	aParams->backupFile = "header_backup.img";
	aParams->resumeFilename = "resume.bin";
}

static void syntax(char **argv, const char *aMessage, enum terminationCode_t aExitCode) {
	if (aMessage) {
		fprintf(stderr, "Error: %s\n", aMessage);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "luksipc: Tool to convert block devices to LUKS-encrypted block devices on the fly\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s (-d, --device=RAWDEV) (--readdev=DEV) (-b, --blocksize=BYTES)\n", argv[0]);
	fprintf(stderr, "    (-c, --backupfile=FILE) (-k, --keyfile=FILE) (-p, --luksparam=PARAMS)\n");
	fprintf(stderr, "    (-l, --loglevel=LVL) (--resume) (--resume-file=FILE) (--no-seatbelt)\n");
	fprintf(stderr, "    (--i-know-what-im-doing) (-h, --help)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -d, --device=RAWDEV        Raw device that is about to be converted to LUKS. This is\n");
	fprintf(stderr, "                             the device that luksFormat will be called on to create the\n");
	fprintf(stderr, "                             new LUKS container. Mandatory argument.\n");
	fprintf(stderr, "      --readdev=DEV          The device that the unencrypted data should be read from.\n");
	fprintf(stderr, "                             This is only different from the raw device if the volume is\n");
	fprintf(stderr, "                             already LUKS (or another container) and you want to\n");
	fprintf(stderr, "                             reLUKSify it.\n");
	fprintf(stderr, "  -b, --blocksize=BYTES      Specify block size for copying in bytes. Default (and\n");
	fprintf(stderr, "                             minimum) size is 10 MiB (10485760 bytes). This value is\n");
	fprintf(stderr, "                             rounded up to closest 4096-byte value automatically. It must\n");
	fprintf(stderr, "                             be at least size of LUKS header (usually 2048 kiB, but may\n");
	fprintf(stderr, "                             vary).\n");
	fprintf(stderr, "  -c, --backupfile=FILE      Specify the file in which a header backup will be written.\n");
	fprintf(stderr, "                             Essentially the header backup is a dump of the first 128 MiB\n");
	fprintf(stderr, "                             of the raw device. By default this will be written to a file\n");
	fprintf(stderr, "                             named backup.bin.\n");
	fprintf(stderr, "  -k, --keyfile=FILE         Filename for the initial keyfile. A 4096 bytes long file\n");
	fprintf(stderr, "                             will be generated under this location which has /dev/urandom\n");
	fprintf(stderr, "                             as the input. It will be added as the first keyslot in the\n");
	fprintf(stderr, "                             luksFormat process. If you put this file on a volatile\n");
	fprintf(stderr, "                             device such as /dev/shm, remember that all your data is\n");
	fprintf(stderr, "                             garbage after a reboot if you forget to add a second key to\n");
	fprintf(stderr, "                             the LUKS keyring. The default filename is\n");
	fprintf(stderr, "                             /root/initial_keyfile.bin. This file will always be created\n");
	fprintf(stderr, "                             with 0o600 permissions.\n");
	fprintf(stderr, "  -p, --luksparam=PARAMS     Pass these additional options to luksFormat, for example to\n");
	fprintf(stderr, "                             select a different cipher. Parameters have to be passed\n");
	fprintf(stderr, "                             comma-separated.\n");
	fprintf(stderr, "  -l, --loglevel=LVL         Integer value that specifies the level of logging verbosity\n");
	fprintf(stderr, "                             from 0 to 4 (critical, error, warn, info, debug). Default\n");
	fprintf(stderr, "                             loglevel is 3 (info).\n");
	fprintf(stderr, "      --resume               Resume a interrupted conversion with the help of a resume\n");
	fprintf(stderr, "                             file. This file is generated when luksipc aborts, is by\n");
	fprintf(stderr, "                             default called resume.bin (this can be changed by --resume-\n");
	fprintf(stderr, "                             file).\n");
	fprintf(stderr, "      --resume-file=FILE     Change the file name from which the resume information is\n");
	fprintf(stderr, "                             read (when resuming a previously aborted conversion) and to\n");
	fprintf(stderr, "                             which resume information is written (in the case of an\n");
	fprintf(stderr, "                             abort). By default this will be resume.bin.\n");
	fprintf(stderr, "      --no-seatbelt          Disable several safetly checks which are in place to keep\n");
	fprintf(stderr, "                             you from losing data. You really need to know what you're\n");
	fprintf(stderr, "                             doing if you use this.\n");
	fprintf(stderr, "      --i-know-what-im-doing Enable batch mode (will not ask any questions or\n");
	fprintf(stderr, "                             confirmations interactively). Please note that you will have\n");
	fprintf(stderr, "                             to perform any and all sanity checks by yourself if you use\n");
	fprintf(stderr, "                             this option in order to avoid losing data.\n");
	fprintf(stderr, "  -h, --help                 Show this help screen.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "    %s -d /dev/sda9\n", argv[0]);
	fprintf(stderr, "       Converts /dev/sda9 to a LUKS partition with default parameters.\n");
	fprintf(stderr, "    %s -d /dev/sda9 --resume-file myresume.dat\n", argv[0]);
	fprintf(stderr, "       Converts /dev/sda9 to a LUKS partition with default parameters and store resume\n");
	fprintf(stderr, "       information in myresume.dat in case of an abort.\n");
	fprintf(stderr, "    %s -d /dev/sda9 -k /root/secure_key/keyfile.bin --luksparams='-c,twofish-lrw-benbi,-s,320,-h,sha256'\n", argv[0]);
	fprintf(stderr, "       Converts /dev/sda9 to a LUKS partition and stores the initially used keyfile in\n");
	fprintf(stderr, "       /root/secure_key/keyfile.bin. Additionally some LUKS parameters are passed that\n");
	fprintf(stderr, "       specify that the Twofish cipher should be used with a 320 bit keysize and\n");
	fprintf(stderr, "       SHA-256 as a hash function.\n");
	fprintf(stderr, "    %s -d /dev/sda9 --resume --resume-file /root/resume.bin\n", argv[0]);
	fprintf(stderr, "       Resumes a crashed LUKS conversion of /dev/sda9 using the file /root/resume.bin\n");
	fprintf(stderr, "       which was generated at the first (crashed) luksipc run.\n");
	fprintf(stderr, "    %s -d /dev/sda9 --readdev /dev/mapper/oldluks\n", argv[0]);
	fprintf(stderr, "       Convert the raw device /dev/sda9, which is already a LUKS container, to a new\n");
	fprintf(stderr, "       LUKS container. For example, this can be used to change the encryption\n");
	fprintf(stderr, "       parameters of the LUKS container (different cipher) or to change the bulk\n");
	fprintf(stderr, "       encryption key. In this example the old container is unlocked and accessible\n");
	fprintf(stderr, "       under /dev/mapper/oldluks.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "luksipc version: " LUKSIPC_VERSION "\n");
#ifdef DEVELOPMENT
	fprintf(stderr, "\n");
	fprintf(stderr, "WARNING: You're using a development build of luksipc. This is not recommended\n");
	fprintf(stderr, "unless you're actually doing software development of luksipc.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Additional (undocumented) options for development release:\n");
	fprintf(stderr, "    --development-slowdown\n");
	fprintf(stderr, "    --development-ioerrors\n");
#endif
	terminate(aExitCode);
}

static void checkParameters(char **argv, const struct conversionParameters *aParams) {
	char errorMessage[256];
	if (!aParams->readDevice) {
		syntax(argv, "No device to convert was given on the command line", EC_CMDLINE_ARGUMENT_ERROR);
	}
	if ((aParams->luksFormatParams) && ((strlen(aParams->luksFormatParams) + 1) > MAX_ARGLENGTH)) {
		snprintf(errorMessage, sizeof(errorMessage), "Length of LUKS format parameters exceeds maximum of %d.", MAX_ARGLENGTH);
		syntax(argv, errorMessage, EC_CMDLINE_ARGUMENT_ERROR);
	}
	if (aParams->blocksize < MINBLOCKSIZE) {
		snprintf(errorMessage, sizeof(errorMessage), "Blocksize needs to be at the very least %d bytes (size of LUKS header), user specified %d bytes.", MINBLOCKSIZE, aParams->blocksize);
		syntax(argv, errorMessage, EC_CMDLINE_ARGUMENT_ERROR);
	}
	if ((aParams->logLevel < 0) || (aParams->logLevel > LLVL_DEBUG)) {
		snprintf(errorMessage, sizeof(errorMessage), "Loglevel needs to be inbetween 0 and %d, user specified %d.", LLVL_DEBUG, aParams->logLevel);
		syntax(argv, errorMessage, EC_CMDLINE_ARGUMENT_ERROR);
	}
}

enum longOnlyOptions_t {
	OPT_IKNOWWHATIMDOING = 0x1000,
	OPT_RESUME,
	OPT_RESUME_FILE,
	OPT_READDEVICE,
	OPT_NOSEATBELT,
#ifdef DEVELOPMENT
	OPT_DEV_IOERRORS,
	OPT_DEV_SLOWDOWN
#endif
};

void parseParameters(struct conversionParameters *aParams, int argc, char **argv) {
	struct option longOptions[] = {
		{ "device", 1, NULL, 'd' },
		{ "readdev", 1, NULL, OPT_READDEVICE },
		{ "blocksize", 1, NULL, 'b' },
		{ "backupfile", 1, NULL, 'c' },
		{ "keyfile", 1, NULL, 'k' },
		{ "luksparams", 1, NULL, 'p' },
		{ "loglevel", 1, NULL, 'l' },
		{ "resume", 0, NULL, OPT_RESUME },
		{ "resume-file", 1, NULL, OPT_RESUME_FILE },
		{ "no-seatbelt", 0, NULL, OPT_NOSEATBELT },
		{ "i-know-what-im-doing", 0, NULL, OPT_IKNOWWHATIMDOING },
		{ "i-know-what-im-doinx", 0, NULL, 'h' },							/* Do not allow abbreviation of --i-know-what-im-doing */
#ifdef DEVELOPMENT
		{ "development-slowdown", 0, NULL, OPT_DEV_SLOWDOWN },
		{ "development-slowdowx", 0, NULL, 'h' },							/* Do not allow abbreviation of --development-slowdown */
		{ "development-ioerrors", 0, NULL, OPT_DEV_IOERRORS },
		{ "development-ioerrorx", 0, NULL, 'h' },							/* Do not allow abbreviation of --development-ioerrors */
#endif
		{ "help", 0, NULL, 'h' },
		{ 0 }
	};
	int character;

	defaultParameters(aParams);
	while ((character = getopt_long(argc, argv, "hb:d:l:k:p:c:", longOptions, NULL)) != -1) {
		switch (character) {
			case 'd':
				aParams->rawDevice = optarg;
				break;

			case OPT_READDEVICE:
				aParams->readDevice = optarg;
				break;

			case 'b':
				aParams->blocksize = atoi(optarg);
				break;

			case 'c':
				aParams->backupFile = optarg;
				break;

			case 'k':
				aParams->keyFile = optarg;
				break;

			case 'p':
				aParams->luksFormatParams = optarg;
				break;

			case 'l': {
				char *endPtr = NULL;
				aParams->logLevel = strtol(optarg, &endPtr, 10);
				if ((endPtr == NULL) || (*endPtr != 0)) {
					fprintf(stderr, "Error: Cannot convert the value '%s' you passed as a log level (must be an integer).\n", optarg);
					terminate(EC_CMDLINE_ARGUMENT_ERROR);
				}
				if ((aParams->logLevel < 0) || (aParams->logLevel > 4)) {
					fprintf(stderr, "Error: Log level must be between 0 and 4.\n");
					terminate(EC_CMDLINE_ARGUMENT_ERROR);
				}
				break;
			}
			
			case OPT_RESUME:
				aParams->resuming = true;
				break;

			case OPT_RESUME_FILE:
				aParams->resumeFilename = optarg;
				break;

			case OPT_NOSEATBELT:
				aParams->safetyChecks = false;
				break;

			case OPT_IKNOWWHATIMDOING:
				aParams->batchMode = true;
				break;
#ifdef DEVELOPMENT
			case OPT_DEV_IOERRORS:
				aParams->dev.ioErrors = true;
				break;

			case OPT_DEV_SLOWDOWN:
				aParams->dev.slowDown = true;
				break;
#endif

			case '?':
				fprintf(stderr, "\n");

			case 'h':
				syntax(argv, NULL, EC_SUCCESS);
				break;

			default:
				fprintf(stderr, "Error: Lazy programmer caused bug in getopt parsing (character 0x%x = '%c').\n", character, character);
				terminate(EC_CMDLINE_PARSING_ERROR);
		}
	}

	/* Round up block size to 4096 bytes multiple */
	aParams->blocksize = ((aParams->blocksize + 4095) / 4096) * 4096;

	/* If read device is not set, we're not doing reLUKSification (i.e. read
	 * device = raw device) */
	if (!aParams->readDevice) {
		aParams->readDevice = aParams->rawDevice;
	} else {
		aParams->reluksification = true;
	}

	checkParameters(argv, aParams);
}

