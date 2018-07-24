ALL:
	mkdir -p ../binmockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	./assembler ../binutilities/false.s ../binmockup/false.o
	./assembler ../binutilities/true.s ../binmockup/true.o
	./assembler ../binutilities/helloworld.s ../binmockup/helloworld.o
	./assembler ../binutilities/fib.s ../binmockup/fib.o
	./assembler ../binutilities/echo.s ../binmockup/echo.o

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

