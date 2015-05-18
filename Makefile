.PHONY: all clean test valgrind

EXECUTABLE := luksipc
CC := gcc
CFLAGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -std=c11 -O2 -D_LARGEFILE64_SOURCE -D_XOPEN_SOURCE=500 -Werror

LDFLAGS :=

OBJS := luksipc.o luks.o exec.o chunk.o parameters.o keyfile.o logging.o shutdown.o utils.o

all: $(EXECUTABLE)

clean:
	rm -f $(OBJS) $(EXECUTABLE) initial_keyfile.bin

test: all
	./luksipc

valgrind: all
	valgrind --leak-check=yes ./luksipc

luksipc: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(@) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<
