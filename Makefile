.PHONY: all clean test valgrind

EXECUTABLE := luksipc
BUILD_REVISION := $(shell git describe --abbrev=10 --dirty --always)
CFLAGS := -Wall -Wextra -Wshadow -Wswitch -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Werror=implicit-function-declaration -Werror=format
CFLAGS += -std=c11 -O2 -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=500 -DBUILD_REVISION='"$(BUILD_REVISION)"'
#CFLAGS += -DDEVELOPMENT -g

LDFLAGS :=

OBJS := luksipc.o luks.o exec.o chunk.o parameters.o keyfile.o logging.o shutdown.o utils.o mount.o exit.o random.o

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
