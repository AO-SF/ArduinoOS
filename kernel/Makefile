CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../lib
LFLAGS = -lm

OBJS = binprogmem.o bytecode.o debug.o kernel.o kernelfs.o minifs.o procman.o util.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
