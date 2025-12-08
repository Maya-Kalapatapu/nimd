CC      = gcc
CFLAGS  = -g -Wall -std=c99 -fsanitize=address,undefined -pthread

# default target
all: nimd rawc

nimd: nimd.o game.o ngp.o network.o
	$(CC) $(CFLAGS) -o $@ $^

test: nimd rawc
	./test_nimd.sh

rawc: rawc.o pbuf.o network.o
	$(CC) $(CFLAGS) -o $@ $^

# generic rule for .o files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o nimd rawc
