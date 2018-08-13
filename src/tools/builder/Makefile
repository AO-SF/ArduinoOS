CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../../kernel
LFLAGS = -lm

OBJS = builder.o minifsextra.o ../../kernel/minifs.o

ALL: $(OBJS)
	rm -rf mockups/binmockup/*
	rm -rf mockups/usrbinmockup/*
	rm -rf mockups/homemockup/*
	mkdir -p mockups/binmockup
	mkdir -p mockups/usrbinmockup
	mkdir -p mockups/homemockup

	../../../bin/assembler ../../userspace/bin/fib.s mockups/homemockup/fib
	cp homeexample/* mockups/homemockup
	../../../bin/assembler ../../userspace/bin/tree.s mockups/homemockup/tree
	../../../bin/assembler ../../userspace/bin/bomb.s mockups/homemockup/bomb
	../../../bin/assembler ../../userspace/bin/sokoban.s mockups/homemockup/sokoban
	../../../bin/assembler ../../userspace/bin/blink.s mockups/homemockup/blink

	../../../bin/assembler ../../userspace/bin/cat.s mockups/binmockup/cat
	../../../bin/assembler ../../userspace/bin/false.s mockups/binmockup/false
	../../../bin/assembler ../../userspace/bin/init.s mockups/binmockup/init
	../../../bin/assembler ../../userspace/bin/ls.s mockups/binmockup/ls
	../../../bin/assembler ../../userspace/bin/pwd.s mockups/binmockup/pwd
	../../../bin/assembler ../../userspace/bin/sh.s mockups/binmockup/sh
	../../../bin/assembler ../../userspace/bin/true.s mockups/binmockup/true
	../../../bin/assembler ../../userspace/bin/echo.s mockups/binmockup/echo
	../../../bin/assembler ../../userspace/bin/yes.s mockups/binmockup/yes
	../../../bin/assembler ../../userspace/bin/tty.s mockups/binmockup/tty
	../../../bin/assembler ../../userspace/bin/kill.s mockups/binmockup/kill
	../../../bin/assembler ../../userspace/bin/sleep.s mockups/binmockup/sleep
	../../../bin/assembler ../../userspace/bin/cp.s mockups/binmockup/cp
	../../../bin/assembler ../../userspace/bin/rm.s mockups/binmockup/rm

	../../../bin/assembler ../../userspace/bin/time.s mockups/usrbinmockup/time
	../../../bin/assembler ../../userspace/bin/uptime.s mockups/usrbinmockup/uptime
	../../../bin/assembler ../../userspace/bin/factor.s mockups/usrbinmockup/factor
	../../../bin/assembler ../../userspace/bin/ps.s mockups/usrbinmockup/ps
	../../../bin/assembler ../../userspace/bin/burn.s mockups/usrbinmockup/burn

	$(CPP) $(CFLAGS) $(OBJS) -o ../../../bin/builder $(LFLAGS)

%.o: %.c %.h
	$(CPP) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CPP) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
	rm -rf mockups/binmockup/*
	rm -rf mockups/usrbinmockup/*
	rm -rf mockups/homemockup/*
