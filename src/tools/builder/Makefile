CPP = clang
CFLAGS = -std=gnu11 -Wall -O0 -ggdb3 -I../../kernel
LFLAGS = -lm

OBJS = builder.o minifsextra.o ../../kernel/minifs.o

ALL: $(OBJS)
	rm -rf mockups/binmockup/*
	rm -rf mockups/usrbinmockup/*
	rm -rf mockups/homemockup/*
	rm -rf mockups/etcmockup/*
	rm -rf mockups/usrgamesmockup/*
	mkdir -p mockups/binmockup
	mkdir -p mockups/usrbinmockup
	mkdir -p mockups/homemockup
	mkdir -p mockups/etcmockup
	mkdir -p mockups/usrgamesmockup

	cp ../../userspace/bin/startup.sh mockups/etcmockup/startup
	cp ../../userspace/bin/shutdown.sh mockups/etcmockup/shutdown

	../../../bin/assembler ../../userspace/bin/fib.s mockups/homemockup/fib
	cp homeexample/* mockups/homemockup
	../../../bin/assembler ../../userspace/bin/tree.s mockups/homemockup/tree
	../../../bin/assembler ../../userspace/bin/bomb.s mockups/homemockup/bomb
	../../../bin/assembler ../../userspace/bin/blink.s mockups/homemockup/blink

	cp usrgamesexample/* mockups/usrgamesmockup
	../../../bin/assembler ../../userspace/bin/sokoban.s mockups/usrgamesmockup/sokoban

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
	../../../bin/assembler ../../userspace/bin/shutdown.s mockups/binmockup/shutdown
	../../../bin/assembler ../../userspace/bin/mount.s mockups/binmockup/mount
	../../../bin/assembler ../../userspace/bin/signal.s mockups/binmockup/signal
	../../../bin/assembler ../../userspace/bin/unmount.s mockups/binmockup/unmount

	../../../bin/assembler ../../userspace/bin/time.s mockups/usrbinmockup/time
	../../../bin/assembler ../../userspace/bin/uptime.s mockups/usrbinmockup/uptime
	../../../bin/assembler ../../userspace/bin/factor.s mockups/usrbinmockup/factor
	../../../bin/assembler ../../userspace/bin/ps.s mockups/usrbinmockup/ps
	../../../bin/assembler ../../userspace/bin/burn.s mockups/usrbinmockup/burn
	../../../bin/assembler ../../userspace/bin/man.s mockups/usrbinmockup/man
	../../../bin/assembler ../../userspace/bin/getpin.s mockups/usrbinmockup/getpin
	../../../bin/assembler ../../userspace/bin/setpin.s mockups/usrbinmockup/setpin
	../../../bin/assembler ../../userspace/bin/reset.s mockups/usrbinmockup/reset
	../../../bin/assembler ../../userspace/bin/hash.s mockups/usrbinmockup/hash

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
	rm -rf mockups/etcmockup/*
