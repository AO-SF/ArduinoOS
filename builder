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
rm -rf ./tmp/mockups/usrdataloggermockup/*
rm -rf ./tmp/mockups/usrman1mockup/*
rm -rf ./tmp/mockups/usrman2mockup/*
rm -rf ./tmp/mockups/usrman3mockup/*
rm -rf ./tmp/mockups/usrman6mockup/*
mkdir -p ./tmp/progmemdata
mkdir -p ./tmp/mockups/binmockup
mkdir -p ./tmp/mockups/usrbinmockup
mkdir -p ./tmp/mockups/homemockup
mkdir -p ./tmp/mockups/etcmockup
mkdir -p ./tmp/mockups/usrgamesmockup
mkdir -p ./tmp/mockups/usrdataloggermockup
mkdir -p ./tmp/mockups/usrman1mockup
mkdir -p ./tmp/mockups/usrman2mockup
mkdir -p ./tmp/mockups/usrman3mockup
mkdir -p ./tmp/mockups/usrman6mockup

# Fill mock directories
echo "	Creating /bin mockup..."
./bin/aosf-asm ./src/userspace/bin/cat.s ./tmp/mockups/binmockup/cat
./bin/aosf-asm ./src/userspace/bin/cp.s ./tmp/mockups/binmockup/cp
./bin/aosf-asm ./src/userspace/bin/echo.s ./tmp/mockups/binmockup/echo
./bin/aosf-asm ./src/userspace/bin/false.s ./tmp/mockups/binmockup/false
./bin/aosf-asm ./src/userspace/bin/init.s ./tmp/mockups/binmockup/init
./bin/aosf-asm ./src/userspace/bin/kill.s ./tmp/mockups/binmockup/kill
./bin/aosf-asm ./src/userspace/bin/ls.s ./tmp/mockups/binmockup/ls
./bin/aosf-asm ./src/userspace/bin/mount.s ./tmp/mockups/binmockup/mount
./bin/aosf-asm ./src/userspace/bin/pwd.s ./tmp/mockups/binmockup/pwd
./bin/aosf-asm ./src/userspace/bin/rm.s ./tmp/mockups/binmockup/rm
./bin/aosf-asm ./src/userspace/bin/sh.s ./tmp/mockups/binmockup/sh
./bin/aosf-asm ./src/userspace/bin/shutdown.s ./tmp/mockups/binmockup/shutdown
./bin/aosf-asm ./src/userspace/bin/signal.s ./tmp/mockups/binmockup/signal
./bin/aosf-asm ./src/userspace/bin/sleep.s ./tmp/mockups/binmockup/sleep
./bin/aosf-asm ./src/userspace/bin/true.s ./tmp/mockups/binmockup/true
./bin/aosf-asm ./src/userspace/bin/truncate.s ./tmp/mockups/binmockup/truncate
./bin/aosf-asm ./src/userspace/bin/tty.s ./tmp/mockups/binmockup/tty
./bin/aosf-asm ./src/userspace/bin/unmount.s ./tmp/mockups/binmockup/unmount
./bin/aosf-asm ./src/userspace/bin/yes.s ./tmp/mockups/binmockup/yes

echo "	Creating /etc mockup..."
cp ./src/userspace/bin/startup.sh ./tmp/mockups/etcmockup/startup
cp ./src/userspace/bin/shutdown.sh ./tmp/mockups/etcmockup/shutdown

echo "	Creating /home mockup..."
./bin/aosf-asm ./src/userspace/bin/fib.s ./tmp/mockups/homemockup/fib
cp -R ./src/userspace/home ./tmp/mockups/usrbinmockup
./bin/aosf-asm ./src/userspace/bin/bomb.s ./tmp/mockups/homemockup/bomb
./bin/aosf-asm ./src/userspace/bin/blink.s ./tmp/mockups/homemockup/blink
./bin/aosf-asm ./src/userspace/bin/pipetest.s ./tmp/mockups/homemockup/pipetest

echo "	Creating /usr/bin mockup..."
./bin/aosf-asm ./src/userspace/bin/burn.s ./tmp/mockups/usrbinmockup/burn
./bin/aosf-asm ./src/userspace/bin/dht22read.s ./tmp/mockups/usrbinmockup/dht22read
./bin/aosf-asm ./src/userspace/bin/factor.s ./tmp/mockups/usrbinmockup/factor
./bin/aosf-asm ./src/userspace/bin/getpin.s ./tmp/mockups/usrbinmockup/getpin
./bin/aosf-asm ./src/userspace/bin/hash.s ./tmp/mockups/usrbinmockup/hash
./bin/aosf-asm ./src/userspace/bin/hexdump.s ./tmp/mockups/usrbinmockup/hexdump
./bin/aosf-asm ./src/userspace/bin/kloglevel.s ./tmp/mockups/usrbinmockup/kloglevel
./bin/aosf-asm ./src/userspace/bin/lsof.s ./tmp/mockups/usrbinmockup/lsof
./bin/aosf-asm ./src/userspace/bin/man.s ./tmp/mockups/usrbinmockup/man
./bin/aosf-asm ./src/userspace/bin/ps.s ./tmp/mockups/usrbinmockup/ps
./bin/aosf-asm ./src/userspace/bin/reset.s ./tmp/mockups/usrbinmockup/reset
./bin/aosf-asm ./src/userspace/bin/setpin.s ./tmp/mockups/usrbinmockup/setpin
./bin/aosf-asm ./src/userspace/bin/hwdereg.s ./tmp/mockups/usrbinmockup/hwdereg
./bin/aosf-asm ./src/userspace/bin/hwinfo.s ./tmp/mockups/usrbinmockup/hwinfo
./bin/aosf-asm ./src/userspace/bin/hwreg.s ./tmp/mockups/usrbinmockup/hwreg
./bin/aosf-asm ./src/userspace/bin/hwkeypadmnt.s ./tmp/mockups/usrbinmockup/hwkeypadmnt
./bin/aosf-asm ./src/userspace/bin/hwsdmnt.s ./tmp/mockups/usrbinmockup/hwsdmnt
./bin/aosf-asm ./src/userspace/bin/time.s ./tmp/mockups/usrbinmockup/time
./bin/aosf-asm ./src/userspace/bin/uptime.s ./tmp/mockups/usrbinmockup/uptime
./bin/aosf-asm ./src/userspace/bin/watch.s ./tmp/mockups/usrbinmockup/watch
./bin/aosf-asm ./src/userspace/bin/date.s ./tmp/mockups/usrbinmockup/date
./bin/aosf-asm ./src/userspace/bin/fdisk.s ./tmp/mockups/usrbinmockup/fdisk
./bin/aosf-asm ./src/userspace/bin/tree.s ./tmp/mockups/usrbinmockup/tree

echo "	Creating /usr/datalogger mockup..."
./bin/aosf-asm ./src/userspace/bin/dataloggersample.s ./tmp/mockups/usrdataloggermockup/sample
./bin/aosf-asm ./src/userspace/bin/dataloggerview.s ./tmp/mockups/usrdataloggermockup/view

echo "	Creating /usr/games mockup..."
cp ./src/userspace/usrgames/* ./tmp/mockups/usrgamesmockup
./bin/aosf-asm ./src/userspace/bin/sokoban.s ./tmp/mockups/usrgamesmockup/sokoban
./bin/aosf-asm ./src/userspace/bin/highlow.s ./tmp/mockups/usrgamesmockup/highlow

echo "	Creating /usr/man mockups..."
cp ./src/userspace/man/1/* ./tmp/mockups/usrman1mockup
cp ./src/userspace/man/2/* ./tmp/mockups/usrman2mockup
cp ./src/userspace/man/3/* ./tmp/mockups/usrman3mockup
cp ./src/userspace/man/6/* ./tmp/mockups/usrman6mockup

# Install packages
for filename in ./packages/*; do
	cd $filename
	./install "../../"
	cd ../../
done

# Build progmem volumes
echo "	Formatting static PROGMEM data files from userspace files and mockups..."

./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/curses" "_lib_curses" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/pin" "_lib_pin" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/dht22" "_lib_dht22" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/int32" "_lib_std_int32" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/io" "_lib_std_io" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/math" "_lib_std_math" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/mem" "_lib_std_mem" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/proc" "_lib_std_proc" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/rand" "_lib_std_rand" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/spi" "_lib_spi" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/str" "_lib_std_str" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/std/time" "_lib_std_time" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./src/userspace/bin/lib/sys" "_lib_sys" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrman1mockup" "_usr_man_1" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrman2mockup" "_usr_man_2" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrman3mockup" "_usr_man_3" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrman6mockup" "_usr_man_6" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/binmockup" "_bin" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrbinmockup" "_usr_bin" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrgamesmockup" "_usr_games" "./tmp/progmemdata"
./bin/aosf-minifsbuilder -fcheader "./tmp/mockups/usrdataloggermockup" "_usr_datalogger" "./tmp/progmemdata"

echo "	Creating common header file to describe all static PROGMEM data files..."
./builderprogmem

# Build other volumes
echo "	Formatting eeprom data file from /etc and /home mockups..."

./bin/aosf-minifsbuilder --size=1024 -fflatfile "./tmp/mockups/etcmockup" "etc" "./tmp"
./bin/aosf-minifsbuilder --size=3072 -fflatfile "./tmp/mockups/homemockup" "home" "./tmp"

cat ./tmp/etc ./tmp/home > ./eeprom
