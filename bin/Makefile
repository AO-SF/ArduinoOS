ALL:
	mkdir -p ../binmockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	rm -r ../binmockup/*

	./assembler ../binutilities/false.s ../binmockup/false
	./assembler ../binutilities/true.s ../binmockup/true
	./assembler ../binutilities/init.s ../binmockup/init
	./assembler ../binutilities/sh.s ../binmockup/sh
	./assembler ../binutilities/pwd.s ../binmockup/pwd
	./assembler ../binutilities/ls.s ../binmockup/ls
	./assembler ../binutilities/fib.s ../binmockup/fib

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

