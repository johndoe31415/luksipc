#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define staticassert(cond)		_Static_assert(cond, #cond)

#define BLOCK_WORDCNT		32768
#define BLOCK_BYTECNT		(BLOCK_WORDCNT * (int)sizeof(uint64_t))

const uint64_t polynomial = 0xc96c5795d7870f42; // ECMA-182
uint64_t intState = 0xb55dd361fcaa9779;

staticassert(sizeof(long) == 8);

static uint64_t nextValue(void) {
	for (int i = 0; i < 2; i++) {
		if (intState & 1) {
			intState = (intState >> 1) ^ polynomial;
		} else {
			intState = (intState >> 1);
		}
	}
	return intState;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "%s [Bytecount] [(Seed)]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int64_t byteCount = atol(argv[1]);
	if (byteCount < 0) {
		fprintf(stderr, "Bytecount must be nonnegative.\n");
		exit(EXIT_FAILURE);
	}

	if (byteCount == 0) {
		byteCount = ((int64_t)1 << 62);
	}

	if (argc >= 3) {
		intState ^= atol(argv[2]);
	}

	/* Complete blocks first */
	{
		uint64_t bufferBlock[BLOCK_WORDCNT];
		for (int64_t i = 0; i < byteCount / BLOCK_BYTECNT; i++) {
			for (int j = 0; j < BLOCK_WORDCNT; j++) {
				bufferBlock[j] = nextValue();
			}
			fwrite(bufferBlock, BLOCK_BYTECNT, 1, stdout);
		}
		byteCount %= BLOCK_BYTECNT;
	}

	/* Complete words afterwards */
	for (int64_t i = 0; i < byteCount / 8; i++) {
		uint64_t state = nextValue();
		(void)fwrite(&state, 8, 1, stdout);
	}

	/* Then last bytes */
	uint64_t state = nextValue();
	for (int i = 0; i < byteCount % 8; i++) {
		(void)fwrite(&state, 1, 1, stdout);
		state >>= 8;
	}

	return 0;
}
