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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/time.h>
#include <unistd.h>

#include "luksipc.h"
#include "shutdown.h"
#include "logging.h"
#include "exec.h"
#include "luks.h"
#include "parameters.h"
#include "chunk.h"
#include "keyfile.h"
#include "utils.h"

#define staticassert(cond)		_Static_assert(cond, #cond)

/* Assert that lseek(2) has 64-bit file offsets */
staticassert(sizeof(off_t) == 8);

struct copyProgress {
	int rawDevFd, cryptDevFd;
	struct chunk dataBuffer[2];
	int usedBufferIndex;
	uint64_t inOffset, outOffset;
	uint64_t remaining;
	uint64_t totalSize;
	uint64_t copied;
	int resumeFd;
};

static double getTime(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (1e-6 * tv.tv_usec);
}

static uint64_t getDiskSize(int aFd) {
	uint64_t result;
	if (ioctl(aFd, BLKGETSIZE64, &result) == -1) {
		perror("ioctl BLKGETSIZE64");
		exit(EXIT_FAILURE);
	}
	return result;
}

static uint64_t getDiskSizeOfPath(const char *aPath) {
	int fd;
	uint64_t rawDevSize;
	fd = open(aPath, O_RDONLY);
	if (fd == -1) {
		perror("open getDiskSizeOfPath");
		exit(EXIT_FAILURE);
	}
	rawDevSize = getDiskSize(fd);
	close(fd);
	return rawDevSize;
}

static void safeWrite(int aFd, void *aData, int aLength) {
	ssize_t result = write(aFd, aData, aLength);
	if (result != aLength) {
		fprintf(stderr, "Error while trying to write %d bytes to file #%d: only %ld bytes written.\n", aLength, aFd, result);
	}
}

static void safeRead(int aFd, void *aData, int aLength) {
	ssize_t result = read(aFd, aData, aLength);
	if (result != aLength) {
		fprintf(stderr, "Error while trying to read %d bytes to file #%d: only %ld bytes written.\n", aLength, aFd, result);
	}
}

static void writeResumeFile(struct copyProgress *aProgress) {
	safeWrite(aProgress->resumeFd, &aProgress->outOffset, sizeof(uint64_t));
	safeWrite(aProgress->resumeFd, &aProgress->dataBuffer[aProgress->usedBufferIndex].used, sizeof(int));
	safeWrite(aProgress->resumeFd, aProgress->dataBuffer[aProgress->usedBufferIndex].data, aProgress->dataBuffer[aProgress->usedBufferIndex].used);
}

static void showProgress(const struct copyProgress *aProgress, bool aShow) {
	static double startTime = -1;
	static uint64_t lastPos;
	double curTime = getTime();
	if (startTime < 0) {
		startTime = curTime;
		lastPos = aProgress->outOffset;
	} else {
		if (aProgress->outOffset - lastPos >= (100 * 1024 * 1024)) {
			aShow = true;
		}
		if (aShow) {
			double runtimeSeconds = curTime - startTime;
			int runtimeSecondsInteger = (int)runtimeSeconds;

			double copySpeedBytesPerSecond = 0;
			if (runtimeSeconds > 1) {
				copySpeedBytesPerSecond = (double)aProgress->copied / runtimeSeconds;
			}

			uint64_t remainingBytes = aProgress->totalSize - aProgress->outOffset;

			double remainingSecs = 0;
			if (copySpeedBytesPerSecond > 10) {
				remainingSecs = (double)remainingBytes / copySpeedBytesPerSecond;
			}
			int remainingSecsInteger = 0;
			if ((remainingSecs > 0) && (remainingSecs < (100 * 3600))) {
				remainingSecsInteger = (int)remainingSecs;
			}

			logmsg(LLVL_INFO, "%2d:%02d: "
							"%5.1f%%   "
							"%7lu MiB / %lu MiB   "
							"%5.1f MiB/s   "
							"Left: "
							"%7lu MiB "
							"%2d:%02d h:m"
							"\n",
								runtimeSecondsInteger / 3600, runtimeSecondsInteger % 3600 / 60,
								100.0 * (double)aProgress->outOffset / (double)aProgress->totalSize,
								aProgress->outOffset / 1024 / 1024,
								aProgress->totalSize / 1024 / 1024,
								copySpeedBytesPerSecond / 1024. / 1024.,
								remainingBytes / 1024 / 1024,
								remainingSecsInteger / 3600, remainingSecsInteger % 3600 / 60
			);
			lastPos = aProgress->outOffset;
		}
	}
}

/* luksClose the device after we're finsihed */
static bool luksClose(struct copyProgress *aProgress) {
	int result;
	const char *arguments[] = {
		"cryptsetup",
		"luksClose",
		"luksipc",
		NULL
	};

	close(aProgress->rawDevFd);
	close(aProgress->cryptDevFd);

	result = execGetReturnCode(arguments);
	return result == 0;
}

/* Sometimes when closing a LUKS volume, it will fail with something like:
 * "device-mapper: remove ioctl on luksipc failed: Device or resource busy"
 * Therefore we sync disks first here and then try up to three times */
static bool luksCloseRetry(struct copyProgress *aProgress) {
	logmsg(LLVL_INFO, "Synchronizing disks.\n");
	sync();

	logmsg(LLVL_INFO, "Performing luksClose\n");
	bool success = false;
	for (int try = 1; try <= 3; try++) {
		logmsg(LLVL_DEBUG, "luksClose try #%d\n", try);
		success = luksClose(aProgress);
		if (success) {
			break;
		}

		/* Sleep one second and retry */
		sleep(1);
	}
	if (success) {
		logmsg(LLVL_DEBUG, "luksClose successful.\n");
	} else {
		logmsg(LLVL_ERROR, "luksClose failed.\n");
	}
	return success;
}

static bool copyDisk(struct copyProgress *aProgress) {
	bool success = false;
	logmsg(LLVL_INFO, "Starting copying of data...\n");
	while (true) {
		ssize_t bytesTransferred;
		int unUsedBufferIndex = (1 - aProgress->usedBufferIndex);
		int bytesToRead;

		if (aProgress->remaining - (aProgress->dataBuffer[aProgress->usedBufferIndex].used) < aProgress->dataBuffer[unUsedBufferIndex].size) {
			/* Remaining is not a full chunk */
			bytesToRead = aProgress->remaining - (aProgress->dataBuffer[aProgress->usedBufferIndex].used);
			if (bytesToRead > 0) {
				logmsg(LLVL_DEBUG, "Preparing to write last (partial) chunk of %d bytes.\n", bytesToRead);
			}
		} else {
			bytesToRead = aProgress->dataBuffer[unUsedBufferIndex].size;
		}
		if (bytesToRead > 0) {
			bytesTransferred = chunkReadAt(&aProgress->dataBuffer[unUsedBufferIndex], aProgress->rawDevFd, aProgress->inOffset, bytesToRead);
			if (bytesTransferred > 0) {
				aProgress->inOffset += aProgress->dataBuffer[unUsedBufferIndex].used;
			} else {
				logmsg(LLVL_WARN, "Read of %d transferred %d hit EOF at inOffset = %ld remaining = %ld\n", bytesToRead, bytesTransferred, aProgress->inOffset, aProgress->remaining);
			}
		} else {
			if (bytesToRead == 0) {
				logmsg(LLVL_DEBUG, "No more bytes to read, will finish writing last partial chunk of %d bytes.\n", aProgress->remaining);
			} else {
				logmsg(LLVL_WARN, "Odd: %d bytes to read at inOffset = %ld remaining = %ld\n", bytesToRead, aProgress->inOffset, aProgress->remaining);
			}
		}

		if (sigQuit()) {
			logmsg(LLVL_INFO, "Gracefully shutting down.\n");
			writeResumeFile(aProgress);
			exit(EXIT_SUCCESS);
		}

		if (aProgress->remaining < aProgress->dataBuffer[aProgress->usedBufferIndex].used) {
			/* Remaining is not a full chunk */
			aProgress->dataBuffer[aProgress->usedBufferIndex].used = aProgress->remaining;
		}
		bytesTransferred = chunkWriteAt(&aProgress->dataBuffer[aProgress->usedBufferIndex], aProgress->cryptDevFd, aProgress->outOffset);
		if (bytesTransferred > 0) {
			aProgress->remaining -= bytesTransferred;
			aProgress->outOffset += aProgress->dataBuffer[aProgress->usedBufferIndex].used;
			aProgress->copied += bytesTransferred;
			showProgress(aProgress, false);
			if (aProgress->outOffset == aProgress->totalSize) {
				logmsg(LLVL_INFO, "Disk copy completed successfully.\n");
				success = luksCloseRetry(aProgress);
				break;
			}

			aProgress->dataBuffer[aProgress->usedBufferIndex].used = 0;
			aProgress->usedBufferIndex = unUsedBufferIndex;
		}
	}
	return success;
}

static void convert(struct parameters const *parameters) {
	struct copyProgress progress;
	uint64_t rawDevSize, cryptDevSize;
	int hdrSize;
	int i;

	memset(&progress, 0, sizeof(struct copyProgress));

	/* Allocate two block chunks */
	for (i = 0; i < 2; i++) {
		allocChunk(&progress.dataBuffer[i], parameters->blocksize);
	}

	/* Open resume file */
	progress.resumeFd = open("resume.bin", O_TRUNC | O_WRONLY | O_CREAT, 0600);
	if (progress.resumeFd == -1) {
		logmsg(LLVL_ERROR, "open resume.bin for writing: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Truncate resume file to zero and set to size of block */
	if (ftruncate(progress.resumeFd, 0) == -1) {
		logmsg(LLVL_ERROR, "Truncation of resume file failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Write zeros in that resume file to assert we have the necessary disk space available */
	writeResumeFile(&progress);

	/* Then seek to start of resume file in case it needs to be written later on */
	if (lseek(progress.resumeFd, 0, SEEK_SET) == (off_t)-1) {
		logmsg(LLVL_ERROR, "Seek in resume file failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	};

	/* Open unencrypted device */
	progress.rawDevFd = open(parameters->device, O_RDWR);
	if (progress.rawDevFd == -1) {
		logmsg(LLVL_ERROR, "open %s: %s\n", parameters->device, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* And determine its size */
	rawDevSize = getDiskSize(progress.rawDevFd);
	logmsg(LLVL_INFO, "Size of %s is %lu bytes (%lu MiB + %lu bytes)\n", parameters->device, rawDevSize, rawDevSize / (1024 * 1024), rawDevSize % (1024 * 1024));

	/* Read the first chunk of data from the unencrypted device (because it
	 * will be overwritten with the LUKS header after the luksFormat action) */
	logmsg(LLVL_DEBUG, "%s: Reading first chunks.\n", parameters->device);
	if (chunkReadAt(&progress.dataBuffer[0], progress.rawDevFd, 0, progress.dataBuffer[0].size) != parameters->blocksize) {
		logmsg(LLVL_ERROR, "%s: Unable to read chunk data.\n", parameters->device);
		exit(EXIT_FAILURE);
	}
	logmsg(LLVL_DEBUG, "%s: Read %d bytes from first chunk.\n", parameters->device, progress.dataBuffer[0].used);

	/* Check availability of device mapper "luksipc" device */
	{
		int result;
		const char *arguments[] = {
			"cryptsetup",
			"status",
			"luksipc",
			NULL
		};
		logmsg(LLVL_INFO, "Performing dm-crypt status lookup\n");
		result = execGetReturnCode(arguments);
		if (result != 4) {
			logmsg(LLVL_ERROR, "LUKS device 'luksipc' already open, aborting.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Format the device while keeping unencrypted disk header in memory (Chunk 0) */
	{
		int result;
		int argcnt = -1;
		char userSuppliedArguments[MAX_ARGLENGTH];
		const char *arguments[MAX_ARGS] = {
			"cryptsetup",
			"luksFormat",
			"-q",
			"--key-file",
			parameters->keyFile,
			NULL
		};
		if (parameters->luksFormatParams) {
			if (!safestrcpy(userSuppliedArguments, parameters->luksFormatParams, MAX_ARGLENGTH)) {
				logmsg(LLVL_ERROR, "Unable to copy user supplied argument, %d bytes max.\n", MAX_ARGLENGTH);
				exit(EXIT_FAILURE);
			}

			if (!argAppendParse(arguments, userSuppliedArguments, &argcnt, MAX_ARGS)) {
				logmsg(LLVL_ERROR, "Unable to copy user supplied argument, %d count max.\n", MAX_ARGS);
				exit(EXIT_FAILURE);
			}
		}

		if (!argAppend(arguments, parameters->device, &argcnt, MAX_ARGS)) {
			logmsg(LLVL_ERROR, "Unable to copy last user supplied argument, %d count max.\n", MAX_ARGS);
			exit(EXIT_FAILURE);
		}

		logmsg(LLVL_INFO, "Performing luksFormat\n");
		result = execGetReturnCode(arguments);
		if (result != 0) {
			logmsg(LLVL_ERROR, "luksFormat failed, aborting.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* luksOpen the device with the key on disk */
	{
		int result;
		const char *arguments[] = {
			"cryptsetup",
			"luksOpen",
			"--key-file",
			parameters->keyFile,
			parameters->device,
			"luksipc",
			NULL
		};
		logmsg(LLVL_INFO, "Performing luksOpen\n");
		result = execGetReturnCode(arguments);
		if (result != 0) {
			logmsg(LLVL_ERROR, "luksOpen failed, aborting.\n");
			chunkWriteAt(&progress.dataBuffer[0], progress.rawDevFd, 0);		/* Try to unpulp disk */
			exit(EXIT_FAILURE);
		}
	}

	/* Open the encrypted LUKS block device */
	progress.cryptDevFd = open("/dev/mapper/luksipc", O_RDWR);
	if (progress.cryptDevFd == -1) {
		logmsg(LLVL_ERROR, "open luks device failed: %s\n", strerror(errno));
		chunkWriteAt(&progress.dataBuffer[0], progress.rawDevFd, 0);		/* Try to unpulp disk */
		exit(EXIT_FAILURE);
	}

	/* Determine size of LUKS device */
	if (ioctl(progress.cryptDevFd, BLKGETSIZE64, &cryptDevSize) == -1) {
		perror("ioctl BLKGETSIZE64");
		chunkWriteAt(&progress.dataBuffer[0], progress.rawDevFd, 0);		/* Try to unpulp disk */
		exit(EXIT_FAILURE);
	}

	/* Determine size difference and if this is at all possible (if the block
	 * size has been smaller than the header size, the disk is probably screwed
	 * already) */
	logmsg(LLVL_INFO, "Size of cryptodisk is %lu bytes (%lu MiB + %lu bytes)\n", cryptDevSize, cryptDevSize / (1024 * 1024), cryptDevSize % (1024 * 1024));
	{
		hdrSize = rawDevSize - cryptDevSize;
		logmsg(LLVL_INFO, "%d bytes occupied by LUKS header (%d kB + %d bytes)\n", hdrSize, hdrSize / 1024, hdrSize % 1024);
		if (hdrSize > parameters->blocksize) {
			logmsg(LLVL_WARN, "Header size greater than block size, trying to unpulp disk (but probably too late)\n");
			chunkWriteAt(&progress.dataBuffer[0], progress.rawDevFd, 0);		/* Try to unpulp disk */
			exit(EXIT_FAILURE);
		}
	}

	/* Then start the copying process */
	progress.remaining = cryptDevSize;
	progress.totalSize = cryptDevSize;
	progress.usedBufferIndex = 0;
	progress.inOffset = progress.dataBuffer[0].used;
	progress.outOffset = 0;
	copyDisk(&progress);

	/* Give free memory for good measure */
	for (i = 0; i < 2; i++) {
		freeChunk(&progress.dataBuffer[i]);
	}
}

static void resume(struct parameters const *parameters) {
	struct copyProgress progress;
	uint64_t rawDevSize, cryptDevSize;
	int i;

	memset(&progress, 0, sizeof(struct copyProgress));

	/* Allocate two block chunks */
	for (i = 0; i < 2; i++) {
		allocChunk(&progress.dataBuffer[i], parameters->blocksize);
	}

	/* Open resume file */
	progress.resumeFd = open("resume.bin", O_RDWR, 0600);
	if (progress.resumeFd == -1) {
		logmsg(LLVL_ERROR, "open resume.bin for writing: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	safeRead(progress.resumeFd, &progress.outOffset, sizeof(uint64_t));
	safeRead(progress.resumeFd, &progress.dataBuffer[0].used, sizeof(int));
	safeRead(progress.resumeFd, progress.dataBuffer[0].data, progress.dataBuffer[0].used);

	/* Open unencrypted device */
	progress.rawDevFd = open(parameters->device, O_RDWR);
	if (progress.rawDevFd == -1) {
		logmsg(LLVL_ERROR, "open %s: %s\n", parameters->device, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* And determine its size */
	rawDevSize = getDiskSize(progress.rawDevFd);
	logmsg(LLVL_INFO, "Size of %s is %lu bytes (%lu MiB + %lu bytes)\n", parameters->device, rawDevSize, rawDevSize / (1024 * 1024), rawDevSize % (1024 * 1024));

	/* Open the encrypted LUKS block device */
	progress.cryptDevFd = open("/dev/mapper/luksipc", O_RDWR);
	if (progress.cryptDevFd == -1) {
		logmsg(LLVL_ERROR, "open luks device failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Determine size of LUKS device */
	cryptDevSize = getDiskSize(progress.cryptDevFd);

	/* Reset the resume file to zero first -- but do NOT abort on error! */
	if (ftruncate(progress.resumeFd, 0) == -1) {
		logmsg(LLVL_ERROR, "Truncation of resume file failed: %s\n", strerror(errno));
	}
	if (lseek(progress.resumeFd, 0, SEEK_SET) == (off_t)-1) {
		logmsg(LLVL_ERROR, "Seek in truncated resume file failed: %s\n", strerror(errno));
	};

	/* Then start the copying process */
	progress.totalSize = cryptDevSize;
	progress.remaining = cryptDevSize - progress.outOffset;
	progress.inOffset = progress.dataBuffer[0].used + progress.outOffset;
	progress.usedBufferIndex = 0;
	copyDisk(&progress);

	/* Give free memory for good measure */
	for (i = 0; i < 2; i++) {
		freeChunk(&progress.dataBuffer[i]);
	}

}

static void checkPreconditions(struct parameters const *parameters) {
	if (!parameters->resume) {
		logmsg(LLVL_DEBUG, "Checking if device %s is already a LUKS device...\n", parameters->device);
		if (isLuks(parameters->device)) {
			logmsg(LLVL_ERROR, "%s: Already LUKS, refuse to do anything.\n", parameters->device);
			exit(EXIT_FAILURE);
		} else {
			logmsg(LLVL_DEBUG, "%s not yet LUKS, perfect.\n", parameters->device);
		}
	}

	if (!parameters->batchMode) {
		char yes[64];
		uint64_t devSize;
		devSize = getDiskSizeOfPath(parameters->device);
		if (!parameters->resume) {
			fprintf(stderr, "WARNING! All data on %s is to be LUKSified! Ensure that:\n", parameters->device);
			fprintf(stderr, "   1. You have resized the contained filesystem appropriately\n");
			fprintf(stderr, "   2. You have unmounted the contained filesystem\n");
			fprintf(stderr, "   3. You have ensured secure storage of the keyfile that will be generated\n");
			fprintf(stderr, "   4. Power conditions are satisfied (i.e. your laptop is not running off battery)\n");
			fprintf(stderr, "   5. You have a backup of all data on that device\n");
			fprintf(stderr, "\n");
			fprintf(stderr, "    %s: %lu MiB = %.1f GiB\n", parameters->device, devSize / 1024 / 1024, (double)(devSize / 1024 / 1024) / 1024);
			fprintf(stderr, "    Chunk size: %u bytes = %.1f MiB\n", parameters->blocksize, (double)parameters->blocksize / 1024 / 1024);
			fprintf(stderr, "    Keyfile: %s\n", parameters->keyFile);
			fprintf(stderr, "    LUKS format parameters: %s\n", parameters->luksFormatParams ? parameters->luksFormatParams : "None given");
		} else {
			fprintf(stderr, "WARNING! Resume LUKSification of %s requested.\n", parameters->device);
			fprintf(stderr, "   1. The resume file really belongs to the correct disk\n");
			fprintf(stderr, "   2. Power conditions are satisfied (i.e. your laptop is not running off battery)\n");
			fprintf(stderr, "\n");
			fprintf(stderr, "    %s: %lu MiB = %.1f GiB\n", parameters->device, devSize / 1024 / 1024, (double)(devSize / 1024 / 1024) / 1024);
			fprintf(stderr, "    Resume file: resume.bin\n");
		}

		fprintf(stderr, "\n");
		fprintf(stderr, "Are all these conditions satisfied, then answer uppercase yes: ");
		if (!fgets(yes, sizeof(yes) - 1, stdin)) {
			perror("fgets");
			exit(EXIT_FAILURE);
		}
		if (strcmp(yes, "YES\n")) {
			fprintf(stderr, "Wrong answer. Aborting.\n");
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv) {
	struct parameters pgmParameters;
	parseParameters(&pgmParameters, argc, argv);

	/* Check if all preconditions are satisfied */
	checkPreconditions(&pgmParameters);

	/* Then generate the keyfile if we're converting (not in resume mode) */
	if (!pgmParameters.resume) {
		if (!genKeyfile(pgmParameters.keyFile)) {
			logmsg(LLVL_ERROR, "Key generation failed, aborting.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Device could be read and is not LUKS device */
	initSigHdlrs();

	/* See what we should do next */
	if (!pgmParameters.resume) {
		convert(&pgmParameters);
	} else {
		resume(&pgmParameters);
	}

	return 0;
}

