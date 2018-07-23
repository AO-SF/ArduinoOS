ALL:
	cd ../assembler && make
	cd ../disassembler && make
	cd ../emulator && make
	#cd ../kernel && make

	./assembler ./false.s ./false.o
	./assembler ./true.s ./true.o
	./assembler ./helloworld.s ./helloworld.o
	./assembler ./fib.s ./fib.o
	./assembler ./echo.s ./echo.o
clean:
	cd ../assembler && make clean
	cd ../disassembler && make clean
	cd ../emulator && make clean
	cd ../kernel && make clean

