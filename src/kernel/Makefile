CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3
LFLAGS = -lm

OBJS = bytecode.o kernel.o kernelfs.o log.o minifs.o kernelmount.o procman.o wrapper.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
