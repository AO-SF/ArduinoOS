CFLAGS = -std=gnu11 -Wall
LFLAGS = -lm

pc: CFLAGS += -O2 -DPC -ggdb3
pc: CPP = clang
arduino: CFLAGS += -DNDEBUG -Os -flto -mcall-prologues -mmcu=atmega2560 -DF_CPU=16000000UL -DBAUD=9600 -DARDUINO
arduino: CPP = avr-gcc

pc: ALL
arduino: ALL

OBJS = bytecode.o circbuf.o kernel.o kernelfs.o kstr.o log.o minifs.o pins.o kernelmount.o procman.o uart.o wrapper.o

ALL: $(OBJS)
	$(CPP) $(CFLAGS) $(OBJS) -o ../../bin/kernel $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
