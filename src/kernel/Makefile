CFLAGS = -std=gnu11 -Wall
LFLAGS = -lm

pc: CFLAGS += -O2 -DPC
pc: CPP = clang
arduino: CFLAGS += -O3 -mmcu=atmega2560 -DF_CPU=16000000UL -DARDUINO
arduino: CPP = avr-gcc

pc: ALL
arduino: ALL

OBJS = bytecode.o kernel.o kernelfs.o log.o minifs.o kernelmount.o procman.o wrapper.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
