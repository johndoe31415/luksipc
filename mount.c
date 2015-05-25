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
#include <mntent.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "mount.h"
#include "logging.h"

bool isBlockDeviceMounted(const char *aBlkDevice) {
	FILE *f = fopen("/proc/mounts", "r");
	struct mntent *entry;
	bool isMounted = false;
	struct stat blkDevStat;

	if (stat(aBlkDevice, &blkDevStat) != 0)	{
		logmsg(LLVL_ERROR, "Unable to stat %s to determine if it's mounted. Assuming it is mounted for safety. Stat reported: %s\n", aBlkDevice, strerror(errno));
		return true;
	}

	while ((entry = getmntent(f)) != NULL) {
		if (strcmp(entry->mnt_fsname, aBlkDevice) == 0) {
			/* Names match, definitely mounted! */
			logmsg(LLVL_DEBUG, "%s mounted at %s\n", aBlkDevice, entry->mnt_dir);
			isMounted = true;
			break;
		}

		if (strcmp(entry->mnt_fsname, "none")) {
			/* Check major/minor number of device */
			struct stat newDevStat;
			if (stat(entry->mnt_fsname, &newDevStat) == 0) {
				if (newDevStat.st_rdev == blkDevStat.st_rdev) {
					/* Major/minor is identical */
					logmsg(LLVL_DEBUG, "%s has identical struct stat.st_rdev with %s, mounted at %s\n", aBlkDevice, entry->mnt_fsname, entry->mnt_dir);
					isMounted = true;
					break;
				}

			}
		}

	}

	fclose(f);
	return isMounted;
}
