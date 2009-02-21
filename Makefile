#
CC = gcc
SRCS = gc.c
BIN = gc

all: test

gc: $(SRCS)
	$(CC) -g -o $@ $(SRCS)

test: gc
	./gc test

clean:
	rm -f gc
