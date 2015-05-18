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
#include <stdarg.h>

#include "logging.h"

static int currentLogLevel;

int getLogLevel(void) {
	return currentLogLevel;
}

void setLogLevel(int aLogLevel) {
	currentLogLevel = aLogLevel;
}

static const char* logLevelToStr(int aLogLvl) {
	switch (aLogLvl) {
		case LLVL_CRITICAL:	return "C";
		case LLVL_ERROR:	return "E";
		case LLVL_WARN:		return "W";
		case LLVL_INFO:		return "I";
		case LLVL_DEBUG:	return "D";
	}
	return "?";
}

void logmsg(int aLogLvl, const char *aFmtString, ...) {
	if (aLogLvl <= currentLogLevel) {
		va_list ap;
		fprintf(stderr, "[%s]: ", logLevelToStr(aLogLvl));

		va_start(ap, aFmtString);
		vfprintf(stderr, aFmtString, ap);
		va_end(ap);
	}
}
