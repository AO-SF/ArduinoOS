CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../lib
LFLAGS = -lm

OBJS = build.o ../lib/minifs.o ../lib/util.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../bin/build $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
