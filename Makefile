arduino:
	mkdir -p bin
	cd src/kernel && make clean # HACK as kernel may make arduino versions of some of the objects
	cd src/tools/assembler && make
	cd src/tools/disassembler && make
	cd src/tools/emulator && make
	cd src/tools/minifsbuilder && make
	./builder
	cd src/kernel && make clean # HACK as minifsbuilder and others make pc versions of some of the objects
	cd src/kernel && make arduino
	avr-size -C --mcu=atmega2560 ./bin/kernel
	avr-objcopy -O ihex ./bin/kernel ./bin/kernel.hex

pc:
	mkdir -p bin
	cd src/tools/assembler && make
	cd src/tools/disassembler && make
	cd src/tools/emulator && make
	cd src/tools/minifsbuilder && make
	./builder
	cd src/kernel && make pc

upload:
	avrdude -Cavrdude.conf -v -patmega2560 -cwiring -P/dev/ttyACM0 -b115200 -D -Uflash:w:./bin/kernel.hex -U eeprom:w:eeprom

clean:
	cd src/tools/assembler && make clean
	cd src/tools/minifsbuilder && make clean
	cd src/tools/disassembler && make clean
	cd src/tools/emulator && make clean
	cd src/kernel && make clean

