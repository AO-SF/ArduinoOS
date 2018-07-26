ALL:
	mkdir -p ../binmockup

	cd ../assembler && make
	cd ../builder && make
	cd ../disassembler && make
	cd ../emulator && make

	rm -r ../binmockup/*

	./assembler ../binutilities/false.s ../binmockup/false.o
	./assembler ../binutilities/true.s ../binmockup/true.o
	./assembler ../binutilities/helloworld.s ../binmockup/helloworld.o
	./assembler ../binutilities/helloworld2.s ../binmockup/helloworld2.o
	./assembler ../binutilities/fib.s ../binmockup/fib.o
	./assembler ../binutilities/echo.s ../binmockup/echo.o
	./assembler ../binutilities/init.s ../binmockup/init.o

	./builder

	cd ../kernel && make

clean:
	cd ../assembler && make clean
	cd ../builder && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

