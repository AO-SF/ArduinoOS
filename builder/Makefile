CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../lib
LFLAGS = -lm

OBJS = builder.o ../kernel/debug.o ../kernel/kernelfs.o ../kernel/minifs.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../bin/builder $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
