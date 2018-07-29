ALL:
	mkdir -p ../binmockup
	mkdir -p ../homemockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	rm -rf ../binmockup/*
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
	./assembler ../binutilities/time.s ../binmockup/time
	./assembler ../binutilities/echo.s ../binmockup/echo
	./assembler ../binutilities/uptime.s ../binmockup/uptime
	./assembler ../binutilities/yes.s ../binmockup/yes

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

