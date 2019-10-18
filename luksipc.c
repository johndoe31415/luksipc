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
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <inttypes.h>

#include "luksipc.h"
#include "shutdown.h"
#include "logging.h"
#include "exec.h"
#include "luks.h"
#include "parameters.h"
#include "chunk.h"
#include "keyfile.h"
#include "utils.h"
#include "globals.h"
#include "mount.h"
#include "exit.h"
#include "random.h"

#define staticassert(cond)				_Static_assert(cond, #cond)

/* Assert that lseek(2) has 64-bit file offsets */
staticassert(sizeof(off_t) == 8);


#define REMAINING_BYTES(aconvptr)		(((aconvptr)->endOutOffset) - ((aconvptr)->outOffset))

struct conversionProcess {
	int readDevFd, writeDevFd;
	uint64_t readDevSize, writeDevSize;
	struct chunk dataBuffer[2];
	int usedBufferIndex;
	int resumeFd;
	char *rawDeviceAlias;
	bool reluksification;
	uint64_t inOffset, outOffset;
	uint64_t endOutOffset;
	char *writeDeviceHandle;
	char writeDevicePath[48];

	struct {
		double startTime;
		double lastShowTime;
		uint64_t lastOutOffset;
		uint64_t copied;
	} stats;
};

enum copyResult_t {
	COPYRESULT_SUCCESS_FINISHED,
	COPYRESULT_SUCCESS_RESUMABLE,
	COPYRESULT_ERROR_WRITING_RESUME_FILE,
};

static bool checkedWrite(int aFd, void *aData, int aLength) {
	ssize_t result = write(aFd, aData, aLength);
	if (result != aLength) {
		logmsg(LLVL_ERROR, "Error while trying to write %d bytes to file with FD #%d: only %ld bytes written: %s\n", aLength, aFd, result, strerror(errno));
		return false;
	}
	return true;
}

static bool checkedRead(int aFd, void *aData, int aLength) {
	ssize_t result = read(aFd, aData, aLength);
	if (result != aLength) {
		logmsg(LLVL_ERROR, "Error while trying to read %d bytes from file with FD #%d: only %ld bytes read: %s\n", aLength, aFd, result, strerror(errno));
		return false;
	}
	return true;
}

static bool writeResumeFile(struct conversionProcess *aConvProcess) {
	bool success = true;
	char header[RESUME_FILE_HEADER_MAGIC_LEN];
	memcpy(header, RESUME_FILE_HEADER_MAGIC, RESUME_FILE_HEADER_MAGIC_LEN);
	success = (lseek(aConvProcess->resumeFd, 0, SEEK_SET) != -1) && success;
	success = checkedWrite(aConvProcess->resumeFd, header, sizeof(header)) && success;
	success = checkedWrite(aConvProcess->resumeFd, &aConvProcess->outOffset, sizeof(uint64_t)) && success;
	success = checkedWrite(aConvProcess->resumeFd, &aConvProcess->readDevSize, sizeof(uint64_t)) && success;
	success = checkedWrite(aConvProcess->resumeFd, &aConvProcess->writeDevSize, sizeof(uint64_t)) && success;
	success = checkedWrite(aConvProcess->resumeFd, &aConvProcess->reluksification, sizeof(bool)) && success;
	success = checkedWrite(aConvProcess->resumeFd, &aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used, sizeof(uint32_t)) && success;
	success = checkedWrite(aConvProcess->resumeFd, aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].data, aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].size) && success;
	fsync(aConvProcess->resumeFd);
	logmsg(LLVL_DEBUG, "Wrote resume file: read pointer offset %" PRIu64 " write pointer offset %" PRIu64 ", %" PRIu64 " bytes of data in active buffer.\n", aConvProcess->inOffset, aConvProcess->outOffset, aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used);
	return success;
}

static bool readResumeFile(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	bool success = true;
	char header[RESUME_FILE_HEADER_MAGIC_LEN];
	success = (lseek(aConvProcess->resumeFd, 0, SEEK_SET) != -1) && success;
	if (!success) {
		logmsg(LLVL_ERROR, "Seek error while trying to read resume file: %s\n", strerror(errno));
		return false;
	}

	success = checkedRead(aConvProcess->resumeFd, header, sizeof(header)) && success;
	if (!success) {
		logmsg(LLVL_ERROR, "Read error while trying to read resume file header.\n");
		return false;
	}

	if (memcmp(header, RESUME_FILE_HEADER_MAGIC, RESUME_FILE_HEADER_MAGIC_LEN) != 0) {
		logmsg(LLVL_ERROR, "Header magic mismatch in resume file.\n");
		return false;
	}

	uint64_t origReadDevSize, origWriteDevSize;
	bool origReluksification;
	success = checkedRead(aConvProcess->resumeFd, &aConvProcess->outOffset, sizeof(uint64_t)) && success;
	success = checkedRead(aConvProcess->resumeFd, &origReadDevSize, sizeof(uint64_t)) && success;
	success = checkedRead(aConvProcess->resumeFd, &origWriteDevSize, sizeof(uint64_t)) && success;
	success = checkedRead(aConvProcess->resumeFd, &origReluksification, sizeof(bool)) && success;

	if (!success) {
		logmsg(LLVL_ERROR, "Read error while trying to read resume file offset metadata.\n");
		return false;
	}

	if (origReadDevSize != aConvProcess->readDevSize) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Resume file used read device of size %" PRIu64 " bytes, but currently read device size is %" PRIu64 " bytes. Refusing to continue in spite of mismatch.\n", origReadDevSize, aConvProcess->readDevSize);
			return false;
		} else {
			logmsg(LLVL_WARN, "Resume file used read device of size %" PRIu64 " bytes, but currently read device size is %" PRIu64 " bytes. Continuing only because safety checks are disabled.\n", origReadDevSize, aConvProcess->readDevSize);
		}
	}
	if (origWriteDevSize != aConvProcess->writeDevSize) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Resume file used write device of size %" PRIu64 " bytes, but currently write device size is %" PRIu64 " bytes. Refusing to continue in spite of mismatch.\n", origWriteDevSize, aConvProcess->writeDevSize);
			return false;
		} else {
			logmsg(LLVL_WARN, "Resume file used write device of size %" PRIu64 " bytes, but currently write device size is %" PRIu64 " bytes. Continuing only because safety checks are disabled.\n", origWriteDevSize, aConvProcess->writeDevSize);
		}
	}
	if (origReluksification != aConvProcess->reluksification) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Resume file was performing reLUKSification, command line specification indicates you do not want reLUKSification. Refusing to continue in spite of mismatch.\n");
			return false;
		} else {
			logmsg(LLVL_WARN, "Resume file was performing reLUKSification, command line specification indicates you do not want reLUKSification. Continuing only because safety checks are disabled.\n");
		}
	}

	logmsg(LLVL_DEBUG, "Read write pointer offset %" PRIu64 " from resume file.\n", aConvProcess->outOffset);

	aConvProcess->usedBufferIndex = 0;
	success = checkedRead(aConvProcess->resumeFd, &aConvProcess->dataBuffer[0].used, sizeof(uint32_t)) && success;
	success = checkedRead(aConvProcess->resumeFd, aConvProcess->dataBuffer[0].data, aConvProcess->dataBuffer[0].used) && success;

	return success;
}

static void showProgress(struct conversionProcess *aConvProcess) {
	double curTime = getTime();
	if (aConvProcess->stats.startTime < 1) {
		aConvProcess->stats.startTime = curTime;
		aConvProcess->stats.lastOutOffset = aConvProcess->outOffset;
		aConvProcess->stats.lastShowTime = curTime;
	} else {
		uint64_t progressBytes = aConvProcess->outOffset - aConvProcess->stats.lastOutOffset;
		double progressTime = curTime - aConvProcess->stats.lastShowTime;

		bool showStats = ((progressBytes >= 100 * 1024 * 1024) && (progressTime >= 5));
		showStats = showStats || (progressTime >= 60);

		if (showStats) {
			double runtimeSeconds = curTime - aConvProcess->stats.startTime;
			int runtimeSecondsInteger = (int)runtimeSeconds;

			double copySpeedBytesPerSecond = 0;
			if (runtimeSeconds > 1) {
				copySpeedBytesPerSecond = (double)aConvProcess->stats.copied / runtimeSeconds;
			}

			uint64_t remainingBytes = aConvProcess->endOutOffset - aConvProcess->outOffset;

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
							"%7" PRIu64 " MiB / %" PRIu64 " MiB   "
							"%5.1f MiB/s   "
							"Left: "
							"%7" PRIu64 " MiB "
							"%2d:%02d h:m"
							"\n",
								runtimeSecondsInteger / 3600, runtimeSecondsInteger % 3600 / 60,
								100.0 * (double)aConvProcess->outOffset / (double)aConvProcess->endOutOffset,
								aConvProcess->outOffset / 1024 / 1024,
								aConvProcess->endOutOffset / 1024 / 1024,
								copySpeedBytesPerSecond / 1024. / 1024.,
								remainingBytes / 1024 / 1024,
								remainingSecsInteger / 3600, remainingSecsInteger % 3600 / 60
			);
			aConvProcess->stats.lastOutOffset = aConvProcess->outOffset;
			aConvProcess->stats.lastShowTime = curTime;
		}
	}
}

static void closeFileDescriptorsAndSync(struct conversionProcess *aConvProcess) {
	logmsg(LLVL_DEBUG, "Closing read/write file descriptors %d and %d.\n", aConvProcess->readDevFd, aConvProcess->writeDevFd);
	close(aConvProcess->readDevFd);
	close(aConvProcess->writeDevFd);
	aConvProcess->readDevFd = -1;
	aConvProcess->writeDevFd = -1;

	logmsg(LLVL_INFO, "Synchronizing disk...\n");
	sync();
	logmsg(LLVL_INFO, "Synchronizing of disk finished.\n");
}

static enum copyResult_t issueGracefulShutdown(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	logmsg(LLVL_INFO, "Gracefully shutting down.\n");
	if (!writeResumeFile(aConvProcess)) {
		logmsg(LLVL_WARN, "There were errors writing the resume file %s.\n", aParameters->resumeFilename);
		return COPYRESULT_ERROR_WRITING_RESUME_FILE;
	} else {
		logmsg(LLVL_INFO, "Successfully written resume file %s.\n", aParameters->resumeFilename);
		return COPYRESULT_SUCCESS_RESUMABLE;
	}
}

static enum copyResult_t startDataCopy(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	logmsg(LLVL_INFO, "Starting copying of data, read offset %" PRIu64 ", write offset %" PRIu64 "\n", aConvProcess->inOffset, aConvProcess->outOffset);
	while (true) {
		ssize_t bytesTransferred;
		int unUsedBufferIndex = (1 - aConvProcess->usedBufferIndex);
		int bytesToRead;

#ifdef DEVELOPMENT
		if (aParameters->dev.slowDown) {
			usleep(500 * 1000);
		}
#endif

		if (REMAINING_BYTES(aConvProcess) - (aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used) < aConvProcess->dataBuffer[unUsedBufferIndex].size) {
			/* Remaining is not a full chunk */
			bytesToRead = REMAINING_BYTES(aConvProcess) - (aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used);
			if (bytesToRead > 0) {
				logmsg(LLVL_DEBUG, "Preparing to write last (partial) chunk of %d bytes.\n", bytesToRead);
			}
		} else {
			bytesToRead = aConvProcess->dataBuffer[unUsedBufferIndex].size;
		}
		if (bytesToRead > 0) {
#ifdef DEVELOPMENT
			if (aParameters->dev.ioErrors) {
				bytesTransferred = unreliableChunkReadAt(&aConvProcess->dataBuffer[unUsedBufferIndex], aConvProcess->readDevFd, aConvProcess->inOffset, bytesToRead);
			} else {
				bytesTransferred = chunkReadAt(&aConvProcess->dataBuffer[unUsedBufferIndex], aConvProcess->readDevFd, aConvProcess->inOffset, bytesToRead);
			}
#else
			bytesTransferred = chunkReadAt(&aConvProcess->dataBuffer[unUsedBufferIndex], aConvProcess->readDevFd, aConvProcess->inOffset, bytesToRead);
#endif
			if (bytesTransferred == -1) {
				/* Error reading from device, handle this! */
				logmsg(LLVL_ERROR, "Error reading from device at offset 0x%lx, will shutdown.\n", aConvProcess->inOffset);
				issueSigQuit();
			} else if (bytesTransferred > 0) {
				aConvProcess->inOffset += aConvProcess->dataBuffer[unUsedBufferIndex].used;
			} else {
				logmsg(LLVL_WARN, "Read of %d transferred %d hit EOF at inOffset = %ld remaining = %ld\n", bytesToRead, bytesTransferred, aConvProcess->inOffset, REMAINING_BYTES(aConvProcess));
			}
		} else {
			if (bytesToRead == 0) {
				logmsg(LLVL_DEBUG, "No more bytes to read, will finish writing last partial chunk of %d bytes.\n", REMAINING_BYTES(aConvProcess));
			} else {
				logmsg(LLVL_WARN, "Odd: %d bytes to read at inOffset = %ld remaining = %ld\n", bytesToRead, aConvProcess->inOffset, REMAINING_BYTES(aConvProcess));
			}
		}

		if (receivedSigQuit()) {
			return issueGracefulShutdown(aParameters, aConvProcess);
		}

		if (REMAINING_BYTES(aConvProcess) < aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used) {
			/* Remaining is not a full chunk */
			aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used = REMAINING_BYTES(aConvProcess);
		}

#ifdef DEVELOPMENT
		if (aParameters->dev.ioErrors) {
			bytesTransferred = unreliableChunkWriteAt(&aConvProcess->dataBuffer[aConvProcess->usedBufferIndex], aConvProcess->writeDevFd, aConvProcess->outOffset);
		} else {
			bytesTransferred = chunkWriteAt(&aConvProcess->dataBuffer[aConvProcess->usedBufferIndex], aConvProcess->writeDevFd, aConvProcess->outOffset);
		}
#else
		bytesTransferred = chunkWriteAt(&aConvProcess->dataBuffer[aConvProcess->usedBufferIndex], aConvProcess->writeDevFd, aConvProcess->outOffset);
#endif
		if (bytesTransferred == -1) {
			logmsg(LLVL_ERROR, "Error writing to device at offset 0x%lx, shutting down.\n", aConvProcess->outOffset);
			return issueGracefulShutdown(aParameters, aConvProcess);
		} else if (bytesTransferred > 0) {
			aConvProcess->outOffset += bytesTransferred;
			aConvProcess->stats.copied += bytesTransferred;
			showProgress(aConvProcess);
			if (aConvProcess->outOffset == aConvProcess->endOutOffset) {
				logmsg(LLVL_INFO, "Disk copy completed successfully.\n");
				return COPYRESULT_SUCCESS_FINISHED;
			}

			aConvProcess->dataBuffer[aConvProcess->usedBufferIndex].used = 0;
			aConvProcess->usedBufferIndex = unUsedBufferIndex;
		}
	}
}

static bool openResumeFile(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	bool createResumeFile = (!aParameters->resuming);
	int openFlags = createResumeFile ? (O_TRUNC | O_WRONLY | O_CREAT) : O_RDWR;

	/* Open resume file */
	aConvProcess->resumeFd = open(aParameters->resumeFilename, openFlags, 0600);
	if (aConvProcess->resumeFd == -1) {
		logmsg(LLVL_ERROR, "Opening '%s' for %s failed: %s\n", aParameters->resumeFilename, createResumeFile ? "writing" : "reading/writing", strerror(errno));
		return false;
	}

	if (createResumeFile) {
		/* Truncate resume file to zero and set to size of block */
		if (ftruncate(aConvProcess->resumeFd, 0) == -1) {
			logmsg(LLVL_ERROR, "Truncation of resume file failed: %s\n", strerror(errno));
			return false;
		}

		/* Write zeros in that resume file to assert we have the necessary disk
		 * space available */
		if (!writeResumeFile(aConvProcess)) {
			logmsg(LLVL_ERROR, "Error writing the resume file: %s\n", strerror(errno));
			return false;
		}

		/* Then seek to start of resume file in case it needs to be written later on */
		if (lseek(aConvProcess->resumeFd, 0, SEEK_SET) == (off_t)-1) {
			logmsg(LLVL_ERROR, "Seek in resume file failed: %s\n", strerror(errno));
			return false;
		}
	}

	return true;
}

static bool openDevice(const char *aPath, int *aFd, int aOpenFlags, uint64_t *aDeviceSize) {
	/* Open device in requested mode first */
	*aFd = open(aPath, aOpenFlags, 0600);
	if (*aFd == -1) {
		logmsg(LLVL_ERROR, "open %s failed: %s\n", aPath, strerror(errno));
		return false;
	}

	/* Then determine its size */
	*aDeviceSize = getDiskSizeOfFd(*aFd);
	if (*aDeviceSize == 0) {
		logmsg(LLVL_ERROR, "Determine disk size of %s failed: %s\n", aPath, strerror(errno));
		return false;
	}

	return true;
}

static uint64_t absDiff(uint64_t aValue1, uint64_t aValue2) {
	if (aValue1 > aValue2) {
		return aValue1 - aValue2;
	} else {
		return aValue2 - aValue1;
	}
}

/* Determine size difference of the reading and writing devices and if this
 * is at all possible (if the block size has been smaller than the header
 * size, the disk is probably screwed already) */
static bool plausibilizeReadWriteDeviceSizes(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	uint64_t absSizeDiff = absDiff(aConvProcess->readDevSize, aConvProcess->writeDevSize);
	if (absSizeDiff > 0x10000000) {
		logmsg(LLVL_WARN, "Absolute size difference if implausibly large (%" PRIu64 "), something is very wrong.", absSizeDiff);
		return false;
	}

	int32_t hdrSize = aConvProcess->readDevSize - aConvProcess->writeDevSize;
	if (hdrSize > 0) {
		logmsg(LLVL_INFO, "Write disk smaller than read disk by %d bytes (%d kiB + %d bytes, occupied by LUKS header)\n", hdrSize, hdrSize / 1024, hdrSize % 1024);
		if (hdrSize > aParameters->blocksize) {
			logmsg(LLVL_WARN, "LUKS header larger than chunk copy size. LUKS format probably has overwritten data that cannot be recovered.\n");
			return false;
		}
	} else if (hdrSize < 0) {
		logmsg(LLVL_INFO, "Write disk larger than read disk, %d bytes were freed (%d kiB + %d bytes)\n", -hdrSize, -hdrSize / 1024, -hdrSize % 1024);
	} else {
		logmsg(LLVL_INFO, "Write disk size equal to read disk size.\n");
	}
	return true;
}

static bool initializeDeviceAlias(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	aConvProcess->rawDeviceAlias = dmCreateDynamicAlias(aParameters->rawDevice, "luksipc_raw");
	if (!aConvProcess->rawDeviceAlias) {
		logmsg(LLVL_ERROR, "Unable to initialize raw device alias.\n");
		return false;
	}
	logmsg(LLVL_INFO, "Created raw device alias: %s -> %s\n", aParameters->rawDevice, aConvProcess->rawDeviceAlias);
	return true;
}

static bool backupPhysicalDisk(struct conversionParameters const *aParameters, struct conversionProcess *aConvProcess) {
	logmsg(LLVL_INFO, "Backing up physical disk %s header to backup file %s\n", aParameters->rawDevice, aParameters->backupFile);

	if (doesFileExist(aParameters->backupFile)) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Backup file %s already exists, refusing to overwrite.\n", aParameters->backupFile);
			return false;
		} else {
			logmsg(LLVL_WARN, "Backup file %s already exists. Overwriting because safety checks have been disabled.\n", aParameters->backupFile);
		}
	}

	/* Open raw disk for reading (cannot use aConvProcess->readDevFd here since
	 * we might be doing reLUKSification) */
	int readFd = open(aParameters->rawDevice, O_RDONLY);
	if (readFd == -1) {
		logmsg(LLVL_ERROR, "Opening raw disk device %s for reading failed: %s\n", aParameters->rawDevice, strerror(errno));
		return false;
	}

	/* Open backup file */
	int writeFd = open(aParameters->backupFile, O_TRUNC | O_WRONLY | O_CREAT, 0600);
	if (writeFd == -1) {
		logmsg(LLVL_ERROR, "Opening backup file %s for writing failed: %s\n", aParameters->backupFile, strerror(errno));
		return false;
	}

	/* Determine the amount of blocks that need to be copied */
	int copyBlockCount = (HEADER_BACKUP_SIZE_BYTES < aConvProcess->readDevSize) ? HEADER_BACKUP_BLOCKCNT : (aConvProcess->readDevSize / HEADER_BACKUP_BLOCKSIZE);
	logmsg(LLVL_DEBUG, "Backup file %s will consist of %d blocks of %d bytes each (%d bytes total, %d kiB)\n", aParameters->backupFile, copyBlockCount, HEADER_BACKUP_BLOCKSIZE, copyBlockCount * HEADER_BACKUP_BLOCKSIZE, copyBlockCount * HEADER_BACKUP_BLOCKSIZE / 1024);

	/* Start copying */
	uint8_t copyBuffer[HEADER_BACKUP_BLOCKSIZE];
	for (int i = 0; i < copyBlockCount; i++) {
		if (!checkedRead(readFd, copyBuffer, HEADER_BACKUP_BLOCKSIZE)) {
			logmsg(LLVL_ERROR, "Read failed when trying to copy to backup file: %s\n", strerror(errno));
			return false;
		}
		if (!checkedWrite(writeFd, copyBuffer, HEADER_BACKUP_BLOCKSIZE)) {
			logmsg(LLVL_ERROR, "Write failed when trying to copy to backup file: %s\n", strerror(errno));
			return false;
		}
	}

	fsync(writeFd);
	close(writeFd);
	close(readFd);
	return true;
}

static bool generateRandomizedWriteHandle(struct conversionProcess *aConvProcess) {
	strcpy(aConvProcess->writeDevicePath, "/dev/mapper/luksipc_");
	if (!randomHexStrCat(aConvProcess->writeDevicePath, 4)) {
		logmsg(LLVL_ERROR, "Cannot generate randomized luksipc write handle.\n");
		return false;
	}
	aConvProcess->writeDeviceHandle = aConvProcess->writeDevicePath + 12;
	return true;
}

static void convert(struct conversionParameters const *parameters) {
	/* Initialize conversion process status */
	struct conversionProcess convProcess;
	memset(&convProcess, 0, sizeof(struct conversionProcess));

	/* Generate a randomized conversion handle */
	if (!generateRandomizedWriteHandle(&convProcess)) {
		terminate(EC_CANNOT_GENERATE_WRITE_HANDLE);
	}

	/* Initialize device aliases. Actually they're technically only needed for
	 * reLUKSification, but basically are a noop if not using reLUKSification.
	 * To keep the code as simple as possible, we only want to have one case
	 * here */
	if (!initializeDeviceAlias(parameters, &convProcess)) {
		terminate(EC_CANNOT_INITIALIZE_DEVICE_ALIAS);
	}

	/* Allocate two block chunks */
	for (int i = 0; i < 2; i++) {
		if (!allocChunk(&convProcess.dataBuffer[i], parameters->blocksize)) {
			logmsg(LLVL_ERROR, "Failed to allocate chunk buffer %d: %s\n", i, strerror(errno));
			terminate(EC_CANNOT_ALLOCATE_CHUNK_MEMORY);
		}
	}

	/* Open resume file for writing (conversion) or reading/writing (resume) */
	if (!openResumeFile(parameters, &convProcess)) {
		terminate(EC_CANNOT_OPEN_RESUME_FILE);
	}

	/* Open unencrypted device for reading/writing (need write permissions in
	 * case we need to unpulp the disk) */
	if (!openDevice(parameters->readDevice, &convProcess.readDevFd, O_RDWR, &convProcess.readDevSize)) {
		terminate(EC_CANNOT_OPEN_READ_DEVICE);
	}
	logmsg(LLVL_INFO, "Size of reading device %s is %" PRIu64 " bytes (%" PRIu64 " MiB + %" PRIu64 " bytes)\n", parameters->readDevice, convProcess.readDevSize, convProcess.readDevSize / (1024 * 1024), convProcess.readDevSize % (1024 * 1024));

	/* Do a backup of the physical disk first if we're just starting out our
	 * conversion */
	if (!parameters->resuming) {
		if (!backupPhysicalDisk(parameters, &convProcess)) {
			terminate(EC_FAILED_TO_BACKUP_HEADER);
		}
	}

	/* If the whole device is smaller than one copy block, we bail. This would
	 * obviously be possible to handle, but we won't. If your hard disk is so
	 * small, then recreate it. */
	if (convProcess.readDevSize < (uint32_t)parameters->blocksize) {
		logmsg(LLVL_ERROR, "Error: Volume size of %s (%" PRIu64 " bytes) is smaller than chunksize (%u). Weird and unsupported corner case.\n", parameters->readDevice, convProcess.readDevSize, parameters->blocksize);
		terminate(EC_UNSUPPORTED_SMALL_DISK_CORNER_CASE);
	}

	if (!parameters->resuming) {
		/* Read the first chunk of data from the unencrypted device (because it
		 * will be overwritten with the LUKS header after the luksFormat action) */
		logmsg(LLVL_DEBUG, "%s: Reading first chunk.\n", parameters->readDevice);
		if (chunkReadAt(&convProcess.dataBuffer[0], convProcess.readDevFd, 0, convProcess.dataBuffer[0].size) != parameters->blocksize) {
			logmsg(LLVL_ERROR, "%s: Unable to read chunk data.\n", parameters->readDevice);
			terminate(EC_UNABLE_TO_READ_FIRST_CHUNK);
		}
		logmsg(LLVL_DEBUG, "%s: Read %d bytes from first chunk.\n", parameters->readDevice, convProcess.dataBuffer[0].used);

		/* Check availability of device mapper handle before performing format */
		if (!isLuksMapperAvailable(convProcess.writeDeviceHandle)) {
			logmsg(LLVL_ERROR, "Error: luksipc conversion handle '%s' not available.\n", convProcess.writeDeviceHandle);
			terminate(EC_LUKSIPC_WRITE_DEVICE_HANDLE_UNAVAILABLE);
		}

		/* Format the device while keeping unencrypted disk header in memory (Chunk 0) */
		logmsg(LLVL_INFO, "Performing luksFormat of %s\n", parameters->rawDevice);
		if (!luksFormat(convProcess.rawDeviceAlias, parameters->keyFile, parameters->luksFormatParams)) {
			terminate(EC_FAILED_TO_PERFORM_LUKSFORMAT);
		}
	}

	/* luksOpen the writing block device using the generated keyfile */
	logmsg(LLVL_INFO, "Performing luksOpen of %s (opening as mapper name %s)\n", parameters->rawDevice, convProcess.writeDeviceHandle);
	if (!luksOpen(convProcess.rawDeviceAlias, parameters->keyFile, convProcess.writeDeviceHandle)) {
		if (!parameters->resuming) {
			/* Open failed, but we already formatted the disk. Try to unpulp,
			 * but only if we already messed with the disk! */
			chunkWriteAt(&convProcess.dataBuffer[0], convProcess.readDevFd, 0);
		}
		terminate(EC_FAILED_TO_PERFORM_LUKSOPEN);
	}

	/* Open LUKS device for reading/writing */
	if (!openDevice(convProcess.writeDevicePath, &convProcess.writeDevFd, O_RDWR, &convProcess.writeDevSize)) {
		logmsg(LLVL_ERROR, "Opening LUKS device %s failed: %s\n", convProcess.writeDevicePath, strerror(errno));
		if (!parameters->resuming) {
			/* Open failed, but we already formatted the disk. Try to unpulp,
			 * but only if we already messed with the disk! */
			chunkWriteAt(&convProcess.dataBuffer[0], convProcess.readDevFd, 0);
		}
		terminate(EC_FAILED_TO_OPEN_UNLOCKED_CRYPTO_DEVICE);
	}
	logmsg(LLVL_INFO, "Size of luksOpened writing device is %" PRIu64 " bytes (%" PRIu64 " MiB + %" PRIu64 " bytes)\n", convProcess.writeDevSize, convProcess.writeDevSize / (1024 * 1024), convProcess.writeDevSize % (1024 * 1024));

	/* Check that the sizes of reading and writing device are in a sane
	 * relationship to each other (i.e. writing device is maybe slightly
	 * smaller than reading device, but no significant size differences occur).
	 * */
	if (!plausibilizeReadWriteDeviceSizes(parameters, &convProcess)) {
		logmsg(LLVL_ERROR, "Implausible values encountered in regards to disk sizes (readDevSize = %u, writeDevSize = %u), aborting. We're trying to recover the header, but it is incomplete and you should restore from the backup file. DO NOT TRY TO MOUNT THE VOLUME AT THIS POINT IN TIME.\n", convProcess.readDevSize, convProcess.writeDevSize);
		if (!parameters->resuming) {
			/* Open failed, but we already formatted the disk. Try to unpulp
			 * only if we already messed with the disk! We probably have
			 * permapulped the disk at this point ;-( */
			chunkWriteAt(&convProcess.dataBuffer[0], convProcess.readDevFd, 0);
		}
		terminate(EC_DEVICE_SIZES_IMPLAUSIBLE);
	}

	if (!parameters->resuming) {
		convProcess.outOffset = 0;
	} else {
		/* Now it's time to read in the resume file. */
		if (!readResumeFile(parameters, &convProcess)) {
			logmsg(LLVL_ERROR, "Failed to read resume file, aborting.\n");
			terminate(EC_FAILED_TO_READ_RESUME_FILE);
		}
	}

	/* These values are identical for resume and non resume cases */
	convProcess.usedBufferIndex = 0;
	convProcess.endOutOffset = (convProcess.readDevSize < convProcess.writeDevSize) ? convProcess.readDevSize : convProcess.writeDevSize;
	convProcess.inOffset = convProcess.dataBuffer[0].used + convProcess.outOffset;

	/* Then start the copying process */
	enum copyResult_t copyResult = startDataCopy(parameters, &convProcess);
	if (copyResult == COPYRESULT_ERROR_WRITING_RESUME_FILE) {
		terminate(EC_COPY_ABORTED_FAILED_TO_WRITE_WRITE_RESUME_FILE);
	}

	/* Sync the disk and close open file descriptors to partition */
	closeFileDescriptorsAndSync(&convProcess);

	/* Then close the LUKS device */
	if (!dmRemove(convProcess.writeDeviceHandle)) {
		logmsg(LLVL_ERROR, "Failed to close LUKS device %s.\n", convProcess.writeDeviceHandle);
		terminate(EC_FAILED_TO_CLOSE_LUKS_DEVICE);
	}

	/* Finally remove the device mapper alias */
	if (!dmRemove(convProcess.rawDeviceAlias)) {
		logmsg(LLVL_ERROR, "Removing device mapper alias %s failed.\n", convProcess.rawDeviceAlias);
		terminate(EC_FAILED_TO_REMOVE_DEVICE_MAPPER_ALIAS);
	}

	/* Free memory of copy buffers */
	for (int i = 0; i < 2; i++) {
		freeChunk(&convProcess.dataBuffer[i]);
	}

	/* Return with a code that depends on whether the copying was finished
	 * completely or if it was aborted gracefully (i.e. resuming is possible)
	 **/
	terminate((copyResult == COPYRESULT_SUCCESS_FINISHED) ? EC_SUCCESS : EC_COPY_ABORTED_RESUME_FILE_WRITTEN);
}

static void printCheckListItem(int *aNumber, const char *aMsg, ...) {
	(*aNumber)++;
	fprintf(stderr, "    [%d] ", *aNumber);

	va_list argList;
	va_start(argList, aMsg);
	vfprintf(stderr, aMsg, argList);
	va_end(argList);
}

static void checkPreconditions(struct conversionParameters const *aParameters) {
	bool abortProcess = false;
	bool reluksification = strcmp(aParameters->rawDevice, aParameters->readDevice) != 0;

	if ((!aParameters->resuming) && (!reluksification)) {
		logmsg(LLVL_DEBUG, "Checking if device %s is already a LUKS device...\n", aParameters->rawDevice);
		if (isLuks(aParameters->rawDevice)) {
			if (aParameters->safetyChecks) {
				logmsg(LLVL_ERROR, "%s: Already LUKS, refuse to do anything.\n", aParameters->rawDevice);
				abortProcess = true;
			} else {
				logmsg(LLVL_WARN, "%s: Already LUKS. Continuing only because safety checks have been disabled.\n", aParameters->rawDevice);
			}
		} else {
			logmsg(LLVL_DEBUG, "%s: Not yet a LUKS device.\n", aParameters->rawDevice);
		}
	}

	if (!aParameters->resuming) {
		/* Initial conversion, not resuming */
		if (doesFileExist(aParameters->backupFile)) {
			if (aParameters->safetyChecks) {
				logmsg(LLVL_ERROR, "Backup file %s already exists, refusing to overwrite.\n", aParameters->backupFile);
				abortProcess = true;
			} else {
				logmsg(LLVL_WARN, "Backup file %s already exists. Will be overwritten when process continues because safety checks have been disabled.\n", aParameters->backupFile);
			}
		}

		if (doesFileExist(DEFAULT_RESUME_FILENAME)) {
			if (aParameters->safetyChecks) {
				logmsg(LLVL_ERROR, "Resume file %s already exists, refusing to overwrite.\n", DEFAULT_RESUME_FILENAME);
				abortProcess = true;
			} else {
				logmsg(LLVL_WARN, "Resume file %s already exists. Will be overwritten when process continues because safety checks have been disabled.\n", DEFAULT_RESUME_FILENAME);
			}
		}

		if (doesFileExist(aParameters->keyFile)) {
			if (aParameters->safetyChecks) {
				logmsg(LLVL_ERROR, "Key file %s already exists, refusing to overwrite.\n", aParameters->keyFile);
				abortProcess = true;
			} else {
				logmsg(LLVL_WARN, "Key file %s already exists. Will be overwritten when process continues because safety checks have been disabled.\n", aParameters->keyFile);
			}
		}
	}

	if (isBlockDeviceMounted(aParameters->rawDevice)) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Raw block device %s appears to be mounted, refusing to continue.\n", aParameters->rawDevice);
			abortProcess = true;
		} else {
			logmsg(LLVL_WARN, "Raw block device %s appears to be mounted, still continuing because safety checks have been disabled.\n", aParameters->rawDevice);
		}
	}

	if (reluksification && isBlockDeviceMounted(aParameters->readDevice)) {
		if (aParameters->safetyChecks) {
			logmsg(LLVL_ERROR, "Unlocked read block device %s appears to be mounted, refusing to continue.\n", aParameters->readDevice);
			abortProcess = true;
		} else {
			logmsg(LLVL_WARN, "Unlocked read block device %s appears to be mounted, still continuing because safety checks have been disabled.\n", aParameters->readDevice);
		}
	}


	if (abortProcess) {
		terminate(EC_PRECONDITIONS_NOT_SATISFIED);
	}
}

static void askUserConfirmation(struct conversionParameters const *parameters) {
	bool reluksification = strcmp(parameters->rawDevice, parameters->readDevice) != 0;

	if (!parameters->batchMode) {
		uint64_t devSize = getDiskSizeOfPath(parameters->rawDevice);
		if (devSize == 0) {
			logmsg(LLVL_ERROR, "%s: Cannot determine disk size.\n", parameters->rawDevice);
			terminate(EC_UNABLE_TO_GET_RAW_DISK_SIZE);
		}
		fprintf(stderr, "WARNING! luksipc will perform the following actions:\n");
		if (!reluksification) {
			if (!parameters->resuming) {
				fprintf(stderr, "   => Normal LUKSification of plain device %s\n", parameters->rawDevice);
				fprintf(stderr, "   -> luksFormat will be performed on %s\n", parameters->rawDevice);
			} else {
				fprintf(stderr, "   => Resume LUKSification of (partially encrypted) plain device %s\n", parameters->rawDevice);
				fprintf(stderr, "   -> Using the information in resume file %s\n", parameters->resumeFilename);
			}
		} else {
			if (!parameters->resuming) {
				fprintf(stderr, "   => reLUKSification of LUKS device %s\n", parameters->rawDevice);
				fprintf(stderr, "   -> Which has been unlocked at %s\n", parameters->readDevice);
				fprintf(stderr, "   -> luksFormat will be performed on %s\n", parameters->rawDevice);
			} else {
				fprintf(stderr, "   => Resume reLUKSification of (partially re-encrypted) LUKS device %s\n", parameters->rawDevice);
				fprintf(stderr, "   -> Which has been unlocked with the OLD key at %s\n", parameters->readDevice);
				fprintf(stderr, "   -> Using the information in resume file %s\n", parameters->resumeFilename);
			}
		}
		fprintf(stderr, "\n");

		fprintf(stderr, "Please confirm you have completed the checklist:\n");
		int checkPoint = 0;
		if  (!parameters->resuming) {
			printCheckListItem(&checkPoint, "You have resized the contained filesystem(s) appropriately\n");
			printCheckListItem(&checkPoint, "You have unmounted any contained filesystem(s)\n");
			printCheckListItem(&checkPoint, "You will ensure secure storage of the keyfile that will be generated at %s\n", parameters->keyFile);
		} else {
			printCheckListItem(&checkPoint, "The resume file %s belongs to the partially encrypted volume %s\n", parameters->resumeFilename, parameters->rawDevice);
		}
		printCheckListItem(&checkPoint, "Power conditions are satisfied (i.e. your laptop is not running off battery)\n");
		if (!parameters->resuming) {
			printCheckListItem(&checkPoint, "You have a backup of all important data on %s\n", parameters->rawDevice);
		}

		fprintf(stderr, "\n");
		fprintf(stderr, "    %s: %" PRIu64 " MiB = %.1f GiB\n", parameters->rawDevice, devSize / 1024 / 1024, (double)(devSize / 1024 / 1024) / 1024);
		fprintf(stderr, "    Chunk size: %u bytes = %.1f MiB\n", parameters->blocksize, (double)parameters->blocksize / 1024 / 1024);
		fprintf(stderr, "    Keyfile: %s\n", parameters->keyFile);
		fprintf(stderr, "    LUKS format parameters: %s\n", parameters->luksFormatParams ? parameters->luksFormatParams : "None given");
		fprintf(stderr, "    luksipc version: " BUILD_REVISION "\n");
#ifdef DEVELOPMENT
		if (parameters->dev.ioErrors) {
			fprintf(stderr, "    Simulating device I/O errors\n");
		}
		if (parameters->dev.slowDown) {
			fprintf(stderr, "    Simulating slow I/O device\n");
		}
#endif
		fprintf(stderr, "\n");
		fprintf(stderr, "Are all these conditions satisfied, then answer uppercase yes: ");

		char yes[16];
		if (!fgets(yes, sizeof(yes) - 1, stdin)) {
			perror("fgets");
			terminate(EC_UNABLE_TO_READ_FROM_STDIN);
		}
		if (strcmp(yes, "YES\n")) {
			fprintf(stderr, "Wrong answer. Aborting.\n");
			terminate(EC_USER_ABORTED_PROCESS);
		}
	}
}

int main(int argc, char **argv) {
	struct conversionParameters pgmParameters;
	parseParameters(&pgmParameters, argc, argv);

	/* Initialize internal PRNG */
	if (!initPrng()) {
		terminate(EC_PRNG_INITIALIZATION_FAILED);
	}

	/* Set loglevel to value given on command line */
	setLogLevel(pgmParameters.logLevel);

	/* Check if all preconditions are satisfied */
	checkPreconditions(&pgmParameters);

	/* Ask for user confirmation if necessary */
	askUserConfirmation(&pgmParameters);

	/* Then generate the keyfile if we're converting (not in resume mode) */
	if (!pgmParameters.resuming) {
		if (!genKeyfile(pgmParameters.keyFile, !pgmParameters.safetyChecks)) {
			logmsg(LLVL_ERROR, "Key generation failed, aborting.\n");
			terminate(EC_CANNOT_GENERATE_KEY_FILE);
		}
	}

	/* Initialize signal handlers that will take care of abort */
	if (!initSignalHandlers()) {
		terminate(EC_CANNOT_INIT_SIGNAL_HANDLERS);
	}

	/* Then start the actual conversion */
	convert(&pgmParameters);

	return 0;
}

