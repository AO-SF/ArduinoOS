CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3
LFLAGS = -lm

OBJS = diskcreator.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../../bin/aosf-diskcreator $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
