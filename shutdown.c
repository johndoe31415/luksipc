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
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "logging.h"

static bool quit = false;

static void signalInterrupt(int aSignal) {
	(void)aSignal;
	quit = true;
	logmsg(LLVL_CRITICAL, "Shutdown requested by user interrupt, please be patient...\n");
}

bool sigQuit(void) {
	return quit;
}

void initSigHdlrs(void) {
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = signalInterrupt;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;

	if (sigaction(SIGINT, &action, NULL) == -1) {
		fprintf(stderr, "Could not install SIGINT handler: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	if (sigaction(SIGTERM, &action, NULL) == -1) {
		fprintf(stderr, "Could not install SIGTERM handler: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGHUP, &action, NULL) == -1) {
		fprintf(stderr, "Could not install SIGHUP handler: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}
