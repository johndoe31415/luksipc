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

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include <stdbool.h>

#define MINBLOCKSIZE			(1024 * 1024 * 10)

struct conversionParameters {
	int blocksize;
	const char *rawDevice;				/* Partition that the actual LUKS is created on (e.g. /dev/sda9) */
	const char *readDevice;				/* Partition that data is read from (for initial conversion idential to rawDevice, but for reLUKSification maybe /dev/mapper/oldluks) */
	const char *writeDevice;			/* Full path of the file that is written (e.g. /dev/mapper/luksipc) */
	const char *keyFile;
	const char *luksFormatParams;
	const char *writeDeviceHandle;		/* Handle of the device which is to be backed up */
	
	const char *backupFile;				/* File in which header backup is written before luksFormat */
	bool batchMode;
	bool safetyChecks;
	const char *resumeFilename;
	int logLevel;
};

/*************** AUTO GENERATED SECTION FOLLOWS ***************/
void parseParameters(struct conversionParameters *aParams, int argc, char **argv);
/***************  AUTO GENERATED SECTION ENDS   ***************/

#endif

