CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../../kernel -DKTIMENOLOG
LFLAGS = -lm

OBJS = emulator.o ../../kernel/bytecode.o ../../kernel/ktime.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../../bin/aosf-emu $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
