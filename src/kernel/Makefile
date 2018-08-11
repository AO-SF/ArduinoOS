CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3
LFLAGS = -lm

OBJS = bytecode.o kernel.o kernelfs.o log.o minifs.o procman.o progmembin.o progmemlibcurses.o progmemlibstdio.o progmemlibstdmath.o progmemlibstdmem.o progmemlibstdproc.o progmemlibstdstr.o progmemlibstdtime.o progmemusrbin.o wrapper.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)