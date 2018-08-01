CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../lib
LFLAGS = -lm

OBJS = bytecode.o debug.o kernel.o kernelfs.o minifs.o procman.o progmembin.o progmemlibstdio.o progmemlibstdmath.o progmemlibstdmem.o progmemlibstdproc.o progmemlibstdstr.o progmemlibstdtime.o progmemusrbin.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
