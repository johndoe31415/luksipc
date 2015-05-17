.PHONY: all clean

EXECUTABLE := luksipc
CC := gcc
CFLAGS := -Wall -std=c11 -O2 -pthread -g -D_LARGEFILE64_SOURCE

LDFLAGS :=

OBJS := luksipc.o luks.o exec.o chunk.o parameters.o keyfile.o logging.o shutdown.o utils.o

all: $(EXECUTABLE)

clean:
	rm -f $(OBJS) $(EXECUTABLE) initial_keyfile.bin

test: all
	./luksipc

valgrind: all
	valgrind --leak-check=yes ./luksipc

luksipc: $(OBJS) luksipc.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(@) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<
