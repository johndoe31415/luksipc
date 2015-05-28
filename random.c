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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "random.h"
#include "logging.h"

static uint64_t xorShiftState = 0x135b78d8e29a4d5c;

/* Marsaglia Xorshift PRNG */
static uint64_t xorShift64(uint64_t x) {
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

/* This is only used for testing luksipc and is not cryptographically safe in
 * any way. It uses the internal PRNG */
bool randomEvent(uint32_t aOneIn) {
	xorShiftState = xorShift64(xorShiftState);
	return (xorShiftState % aOneIn) == 0;
}

bool readRandomData(void *aData, uint32_t aLength) {
	const char *randomDevice = "/dev/urandom";
	FILE *f = fopen(randomDevice, "rb");
	if (!f) {
		logmsg(LLVL_ERROR, "Error opening %s for reading entropy: %s\n", randomDevice, strerror(errno));
		return false;
	}

	if (fread(aData, aLength, 1, f) != 1) {
		logmsg(LLVL_ERROR, "Short read from %s for reading entropy: %s\n", randomDevice, strerror(errno));
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

bool randomHexStrCat(char *aString, int aByteLen) {
	/* Generate hex data */
	uint8_t rnd[aByteLen];
	if (!readRandomData(rnd, aByteLen)) {
		logmsg(LLVL_ERROR, "Cannot generate randomized hex tag.\n");
		return false;
	}

	/* Walk string until the end */
	aString = aString + strlen(aString);

	/* Then append hex data there */
	for (int i = 0; i < aByteLen; i++) {
		sprintf(aString, "%02x", rnd[i]);
		aString += 2;
	}
	return true;
}

bool initPrng(void) {
	uint64_t xorValue;
	if (!readRandomData(&xorValue, sizeof(xorValue))) {
		logmsg(LLVL_ERROR, "Failed to seed internal PRNG.\n");
		return false;
	}
	xorShiftState ^= xorValue;
	return true;
}

