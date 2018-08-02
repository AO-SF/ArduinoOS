ALL:
	mkdir -p ../binmockup
	mkdir -p ../usrbinmockup
	mkdir -p ../homemockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	rm -rf ../binmockup/*
	rm -rf ../usrbinmockup/*
	rm -rf ../homemockup/*

	./assembler ../binutilities/fib.s ../homemockup/fib
	cp ../binutilities/fib.s ../homemockup/fib.s
	cp ../homefiles/* ../homemockup
	./assembler ../binutilities/tree.s ../homemockup/tree

	./assembler ../binutilities/cat.s ../binmockup/cat
	./assembler ../binutilities/false.s ../binmockup/false
	./assembler ../binutilities/init.s ../binmockup/init
	./assembler ../binutilities/ls.s ../binmockup/ls
	./assembler ../binutilities/pwd.s ../binmockup/pwd
	./assembler ../binutilities/sh.s ../binmockup/sh
	./assembler ../binutilities/true.s ../binmockup/true
	./assembler ../binutilities/echo.s ../binmockup/echo
	./assembler ../binutilities/yes.s ../binmockup/yes
	./assembler ../binutilities/tty.s ../binmockup/tty
	./assembler ../binutilities/kill.s ../binmockup/kill
	./assembler ../binutilities/sleep.s ../binmockup/sleep

	./assembler ../binutilities/time.s ../usrbinmockup/time
	./assembler ../binutilities/uptime.s ../usrbinmockup/uptime
	./assembler ../binutilities/factor.s ../usrbinmockup/factor
	./assembler ../binutilities/ps.s ../usrbinmockup/ps
	./assembler ../binutilities/burn.s ../usrbinmockup/burn
	./assembler ../binutilities/bomb.s ../usrbinmockup/bomb

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

