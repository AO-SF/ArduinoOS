ALL:
	cd src/tools/assembler && make
	cd src/tools/disassembler && make
	cd src/tools/emulator && make
	cd src/tools/builder && make
	./bin/builder
	cd src/kernel && make

clean:
	cd src/tools/assembler && make clean
	cd src/tools/builder && make clean
	cd src/tools/disassembler && make clean
	cd src/tools/emulator && make clean
	cd src/kernel && make clean

