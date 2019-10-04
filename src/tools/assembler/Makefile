CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../../kernel/
LFLAGS = -lm

OBJS = assembler.o util.o ../../kernel/bytecode.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../../bin/aosf-asm $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
