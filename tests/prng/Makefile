.PHONY: all clean

CC := gcc
CFLAGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -std=c11 -Wall -O3

LDFLAGS :=

OBJS := prng_crc64.o

all: prng_crc64

clean:
	rm -f $(OBJS) prng_crc64

test: all
	./prng_crc64 123456789

valgrind: all
	valgrind --leak-check=yes ./prng_crc64

prng_crc64: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(@) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<
