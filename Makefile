#
CC = gcc
SRCS = gc.c
BIN = gc

all: clean gc

clean:
	rm -f gc

gc: $(SRCS)
	$(CC) -g -o gc $(SRCS)

gc_debug:
	$(CC) -g -DDO_DEBUG -O0 -o gc $(SRCS)

test: clean gc_debug
	./gc test
