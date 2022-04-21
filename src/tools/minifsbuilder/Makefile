CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../../kernel
LFLAGS = -lm

OBJS = minifsbuilder.o minifsextra.o ../../kernel/kstr.o ../../kernel/minifs.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../../bin/aosf-minifsbuilder $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
