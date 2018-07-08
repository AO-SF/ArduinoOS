CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3
LFLAGS = -lm

OBJS = minifs.o test.o util.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ./test $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
