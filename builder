#!/bin/bash

# Create mock directories
echo "	Creating mockup directories in tmp..."

rm -rf ./tmp/*
mkdir -p ./tmp

rm -rf ./tmp/progmemdata/*
rm -rf ./tmp/mockups/binmockup/*
rm -rf ./tmp/mockups/usrbinmockup/*
rm -rf ./tmp/mockups/homemockup/*
rm -rf ./tmp/mockups/etcmockup/*
rm -rf ./tmp/mockups/usrgamesmockup/*
mkdir -p ./tmp/progmemdata
mkdir -p ./tmp/mockups/binmockup
mkdir -p ./tmp/mockups/usrbinmockup
mkdir -p ./tmp/mockups/homemockup
mkdir -p ./tmp/mockups/etcmockup
mkdir -p ./tmp/mockups/usrgamesmockup

# Fill mock directories
echo "	Creating /bin mockup..."
./bin/assembler ./src/userspace/bin/cat.s ./tmp/mockups/binmockup/cat
./bin/assembler ./src/userspace/bin/cp.s ./tmp/mockups/binmockup/cp
./bin/assembler ./src/userspace/bin/echo.s ./tmp/mockups/binmockup/echo
./bin/assembler ./src/userspace/bin/false.s ./tmp/mockups/binmockup/false
./bin/assembler ./src/userspace/bin/init.s ./tmp/mockups/binmockup/init
./bin/assembler ./src/userspace/bin/kill.s ./tmp/mockups/binmockup/kill
./bin/assembler ./src/userspace/bin/ls.s ./tmp/mockups/binmockup/ls
./bin/assembler ./src/userspace/bin/mount.s ./tmp/mockups/binmockup/mount
./bin/assembler ./src/userspace/bin/pwd.s ./tmp/mockups/binmockup/pwd
./bin/assembler ./src/userspace/bin/rm.s ./tmp/mockups/binmockup/rm
./bin/assembler ./src/userspace/bin/sh.s ./tmp/mockups/binmockup/sh
./bin/assembler ./src/userspace/bin/shutdown.s ./tmp/mockups/binmockup/shutdown
./bin/assembler ./src/userspace/bin/signal.s ./tmp/mockups/binmockup/signal
./bin/assembler ./src/userspace/bin/sleep.s ./tmp/mockups/binmockup/sleep
./bin/assembler ./src/userspace/bin/true.s ./tmp/mockups/binmockup/true
./bin/assembler ./src/userspace/bin/truncate.s ./tmp/mockups/binmockup/truncate
./bin/assembler ./src/userspace/bin/tty.s ./tmp/mockups/binmockup/tty
./bin/assembler ./src/userspace/bin/unmount.s ./tmp/mockups/binmockup/unmount
./bin/assembler ./src/userspace/bin/yes.s ./tmp/mockups/binmockup/yes

echo "	Creating /etc mockup..."
cp ./src/userspace/bin/startup.sh ./tmp/mockups/etcmockup/startup
cp ./src/userspace/bin/shutdown.sh ./tmp/mockups/etcmockup/shutdown

echo "	Creating /home mockup..."
./bin/assembler ./src/userspace/bin/fib.s ./tmp/mockups/homemockup/fib
cp ./src/userspace/home/* ./tmp/mockups/homemockup
./bin/assembler ./src/userspace/bin/helloworld.s ./tmp/mockups/homemockup/helloworld
./bin/assembler ./src/userspace/bin/tree.s ./tmp/mockups/homemockup/tree
./bin/assembler ./src/userspace/bin/bomb.s ./tmp/mockups/homemockup/bomb
./bin/assembler ./src/userspace/bin/blink.s ./tmp/mockups/homemockup/blink
./bin/assembler ./src/userspace/bin/blinkfast.s ./tmp/mockups/homemockup/blinkfast

echo "	Creating /usr/bin mockup..."
./bin/assembler ./src/userspace/bin/burn.s ./tmp/mockups/usrbinmockup/burn
./bin/assembler ./src/userspace/bin/dht22read.s ./tmp/mockups/usrbinmockup/dht22read
./bin/assembler ./src/userspace/bin/factor.s ./tmp/mockups/usrbinmockup/factor
./bin/assembler ./src/userspace/bin/getpin.s ./tmp/mockups/usrbinmockup/getpin
./bin/assembler ./src/userspace/bin/hash.s ./tmp/mockups/usrbinmockup/hash
./bin/assembler ./src/userspace/bin/hexdump.s ./tmp/mockups/usrbinmockup/hexdump
./bin/assembler ./src/userspace/bin/kloglevel.s ./tmp/mockups/usrbinmockup/kloglevel
./bin/assembler ./src/userspace/bin/lsof.s ./tmp/mockups/usrbinmockup/lsof
./bin/assembler ./src/userspace/bin/man.s ./tmp/mockups/usrbinmockup/man
./bin/assembler ./src/userspace/bin/ps.s ./tmp/mockups/usrbinmockup/ps
./bin/assembler ./src/userspace/bin/reset.s ./tmp/mockups/usrbinmockup/reset
./bin/assembler ./src/userspace/bin/setpin.s ./tmp/mockups/usrbinmockup/setpin
./bin/assembler ./src/userspace/bin/hwdereg.s ./tmp/mockups/usrbinmockup/hwdereg
./bin/assembler ./src/userspace/bin/hwinfo.s ./tmp/mockups/usrbinmockup/hwinfo
./bin/assembler ./src/userspace/bin/hwreg.s ./tmp/mockups/usrbinmockup/hwreg
./bin/assembler ./src/userspace/bin/hwsdmnt.s ./tmp/mockups/usrbinmockup/hwsdmnt
./bin/assembler ./src/userspace/bin/time.s ./tmp/mockups/usrbinmockup/time
./bin/assembler ./src/userspace/bin/uptime.s ./tmp/mockups/usrbinmockup/uptime
./bin/assembler ./src/userspace/bin/watch.s ./tmp/mockups/usrbinmockup/watch

echo "	Creating /usr/games mockup..."
cp ./src/userspace/usrgames/* ./tmp/mockups/usrgamesmockup
./bin/assembler ./src/userspace/bin/sokoban.s ./tmp/mockups/usrgamesmockup/sokoban

# Build progmem volumes
echo "	Formatting static PROGMEM data files from userspace files and mockups..."

./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/curses" "_lib_curses" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/pin" "_lib_pin" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/dht22" "_lib_std_dht22" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/io" "_lib_std_io" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/math" "_lib_std_math" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/mem" "_lib_std_mem" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/proc" "_lib_std_proc" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/spi" "_lib_std_spi" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/str" "_lib_std_str" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/time" "_lib_std_time" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/sys" "_lib_sys" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/man/1" "_usr_man_1" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/man/2" "_usr_man_2" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./src/userspace/man/3" "_usr_man_3" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./tmp/mockups/binmockup" "_bin" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./tmp/mockups/usrbinmockup" "_usr_bin" "./tmp/progmemdata"
./bin/minifsbuilder -fcheader "./tmp/mockups/usrgamesmockup" "_usr_games" "./tmp/progmemdata"

echo "	Creating common header file to describe all static PROGMEM data files..."
./builderprogmem

# Build other volumes
echo "	Formatting eeprom data file from /etc and /home mockups..."

./bin/minifsbuilder --size=1024 -fflatfile "./tmp/mockups/etcmockup" "etc" "./tmp"
./bin/minifsbuilder --size=3072 -fflatfile "./tmp/mockups/homemockup" "home" "./tmp"

cat ./tmp/etc ./tmp/home > ./eeprom
