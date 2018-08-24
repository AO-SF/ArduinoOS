arduino:
	mkdir -p bin
	cd src/kernel && make clean # HACK as kernel may make arduino versions of some of the objects
	cd src/tools/assembler && make
	cd src/tools/disassembler && make
	cd src/tools/emulator && make
	cd src/tools/builder && make
	./bin/builder --compact
	cd src/kernel && make clean # HACK as builder and others make pc versions of some of the objects
	cd src/kernel && make arduino
	avr-objcopy -O ihex ./bin/kernel ./bin/kernel.hex

pc:
	mkdir -p bin
	cd src/tools/assembler && make
	cd src/tools/disassembler && make
	cd src/tools/emulator && make
	cd src/tools/builder && make
	./bin/builder --compact
	cd src/kernel && make pc

clean:
	cd src/tools/assembler && make clean
	cd src/tools/builder && make clean
	cd src/tools/disassembler && make clean
	cd src/tools/emulator && make clean
	cd src/kernel && make clean

