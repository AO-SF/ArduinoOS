ALL:
	@echo "Error expected argument - try 'make arduino', 'make pc' or 'make clean'"
	@exit

arduino:
	@echo "Compiling tools..."
	@mkdir -p bin
	@cd src/kernel && make --quiet clean # HACK as kernel may make arduino versions of some of the objects
	@cd src/tools/assembler && make --quiet
	@cd src/tools/disassembler && make --quiet
	@cd src/tools/emulator && make --quiet
	@cd src/tools/minifsbuilder && make --quiet
	@echo "Running builder script..."
	@./builder
	@echo "Compiling kernel..."
	@cd src/kernel && make --quiet clean # HACK as minifsbuilder and others make pc versions of some of the objects
	@cd src/kernel && make --quiet arduino
	@echo "Creating hex file from kernel executable..."
	@avr-objcopy -O ihex ./bin/kernel ./bin/kernel.hex
	@avr-size -C --mcu=atmega2560 ./bin/kernel

pc:
	@echo "Compiling tools..."
	@mkdir -p bin
	@cd src/tools/assembler && make --quiet
	@cd src/tools/disassembler && make --quiet
	@cd src/tools/emulator && make --quiet
	@cd src/tools/minifsbuilder && make --quiet
	@echo "Running builder script..."
	@./builder
	@echo "Compiling kernel..."
	@cd src/kernel && make --quiet pc

install:
	@mkdir -p /usr/include/aosf-stdlib
	@cp -f -r src/userspace/bin/lib /usr/include/aosf-stdlib
	@cp bin/aosf-asm /usr/local/bin
	@cp bin/aosf-disassembler /usr/local/bin
	@cp bin/aosf-emu /usr/local/bin
	@cp bin/aosf-minifsbuilder /usr/local/bin

upload:
	avrdude -Cavrdude.conf -v -patmega2560 -cwiring -P/dev/ttyACM0 -b115200 -D -Uflash:w:./bin/kernel.hex -U eeprom:w:eeprom

clean:
	@cd src/tools/assembler && make --quiet clean
	@cd src/tools/minifsbuilder && make --quiet clean
	@cd src/tools/disassembler && make --quiet clean
	@cd src/tools/emulator && make --quiet clean
	@cd src/kernel && make --quiet clean
	@rm -rf ./tmp/*
