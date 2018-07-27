ALL:
	mkdir -p ../binmockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	rm -r ../binmockup/*

	./assembler ../binutilities/false.s ../binmockup/false.o
	./assembler ../binutilities/true.s ../binmockup/true.o
	./assembler ../binutilities/init.s ../binmockup/init.o
	./assembler ../binutilities/sh.s ../binmockup/sh.o

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

