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

	../../../bin/assembler ../../userspace/fib.s mockups/homemockup/fib
	cp homeexample/* mockups/homemockup
	../../../bin/assembler ../../userspace/tree.s mockups/homemockup/tree
	../../../bin/assembler ../../userspace/bomb.s mockups/homemockup/bomb
	../../../bin/assembler ../../userspace/sokoban.s mockups/homemockup/sokoban
	../../../bin/assembler ../../userspace/rpg/rpg.s mockups/homemockup/rpg

	../../../bin/assembler ../../userspace/cat.s mockups/binmockup/cat
	../../../bin/assembler ../../userspace/false.s mockups/binmockup/false
	../../../bin/assembler ../../userspace/init.s mockups/binmockup/init
	../../../bin/assembler ../../userspace/ls.s mockups/binmockup/ls
	../../../bin/assembler ../../userspace/pwd.s mockups/binmockup/pwd
	../../../bin/assembler ../../userspace/sh.s mockups/binmockup/sh
	../../../bin/assembler ../../userspace/true.s mockups/binmockup/true
	../../../bin/assembler ../../userspace/echo.s mockups/binmockup/echo
	../../../bin/assembler ../../userspace/yes.s mockups/binmockup/yes
	../../../bin/assembler ../../userspace/tty.s mockups/binmockup/tty
	../../../bin/assembler ../../userspace/kill.s mockups/binmockup/kill
	../../../bin/assembler ../../userspace/sleep.s mockups/binmockup/sleep
	../../../bin/assembler ../../userspace/cp.s mockups/binmockup/cp

	../../../bin/assembler ../../userspace/time.s mockups/usrbinmockup/time
	../../../bin/assembler ../../userspace/uptime.s mockups/usrbinmockup/uptime
	../../../bin/assembler ../../userspace/factor.s mockups/usrbinmockup/factor
	../../../bin/assembler ../../userspace/ps.s mockups/usrbinmockup/ps
	../../../bin/assembler ../../userspace/burn.s mockups/usrbinmockup/burn

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
