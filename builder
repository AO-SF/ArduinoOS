#!/bin/sh

# Create mock directories
rm -rf ./tmp/*
mkdir -p ./tmp

rm -rf ./tmp/mockups/binmockup/*
rm -rf ./tmp/mockups/usrbinmockup/*
rm -rf ./tmp/mockups/homemockup/*
rm -rf ./tmp/mockups/etcmockup/*
rm -rf ./tmp/mockups/usrgamesmockup/*
mkdir -p ./tmp/mockups/binmockup
mkdir -p ./tmp/mockups/usrbinmockup
mkdir -p ./tmp/mockups/homemockup
mkdir -p ./tmp/mockups/etcmockup
mkdir -p ./tmp/mockups/usrgamesmockup

# Fill mock directories
cp ./src/userspace/bin/startup.sh ./tmp/mockups/etcmockup/startup
cp ./src/userspace/bin/shutdown.sh ./tmp/mockups/etcmockup/shutdown

./bin/assembler ./src/userspace/bin/fib.s ./tmp/mockups/homemockup/fib
cp ./src/userspace/home/* ./tmp/mockups/homemockup
./bin/assembler ./src/userspace/bin/tree.s ./tmp/mockups/homemockup/tree
./bin/assembler ./src/userspace/bin/bomb.s ./tmp/mockups/homemockup/bomb
./bin/assembler ./src/userspace/bin/blink.s ./tmp/mockups/homemockup/blink
./bin/assembler ./src/userspace/bin/blinkfast.s ./tmp/mockups/homemockup/blinkfast

cp ./src/userspace/usrgames/* ./tmp/mockups/usrgamesmockup
./bin/assembler ./src/userspace/bin/sokoban.s ./tmp/mockups/usrgamesmockup/sokoban

./bin/assembler ./src/userspace/bin/cat.s ./tmp/mockups/binmockup/cat
./bin/assembler ./src/userspace/bin/false.s ./tmp/mockups/binmockup/false
./bin/assembler ./src/userspace/bin/init.s ./tmp/mockups/binmockup/init
./bin/assembler ./src/userspace/bin/ls.s ./tmp/mockups/binmockup/ls
./bin/assembler ./src/userspace/bin/pwd.s ./tmp/mockups/binmockup/pwd
./bin/assembler ./src/userspace/bin/sh.s ./tmp/mockups/binmockup/sh
./bin/assembler ./src/userspace/bin/true.s ./tmp/mockups/binmockup/true
./bin/assembler ./src/userspace/bin/echo.s ./tmp/mockups/binmockup/echo
./bin/assembler ./src/userspace/bin/yes.s ./tmp/mockups/binmockup/yes
./bin/assembler ./src/userspace/bin/tty.s ./tmp/mockups/binmockup/tty
./bin/assembler ./src/userspace/bin/kill.s ./tmp/mockups/binmockup/kill
./bin/assembler ./src/userspace/bin/sleep.s ./tmp/mockups/binmockup/sleep
./bin/assembler ./src/userspace/bin/cp.s ./tmp/mockups/binmockup/cp
./bin/assembler ./src/userspace/bin/rm.s ./tmp/mockups/binmockup/rm
./bin/assembler ./src/userspace/bin/shutdown.s ./tmp/mockups/binmockup/shutdown
./bin/assembler ./src/userspace/bin/mount.s ./tmp/mockups/binmockup/mount
./bin/assembler ./src/userspace/bin/signal.s ./tmp/mockups/binmockup/signal
./bin/assembler ./src/userspace/bin/unmount.s ./tmp/mockups/binmockup/unmount
./bin/assembler ./src/userspace/bin/truncate.s ./tmp/mockups/binmockup/truncate

./bin/assembler ./src/userspace/bin/time.s ./tmp/mockups/usrbinmockup/time
./bin/assembler ./src/userspace/bin/uptime.s ./tmp/mockups/usrbinmockup/uptime
./bin/assembler ./src/userspace/bin/factor.s ./tmp/mockups/usrbinmockup/factor
./bin/assembler ./src/userspace/bin/ps.s ./tmp/mockups/usrbinmockup/ps
./bin/assembler ./src/userspace/bin/burn.s ./tmp/mockups/usrbinmockup/burn
./bin/assembler ./src/userspace/bin/man.s ./tmp/mockups/usrbinmockup/man
./bin/assembler ./src/userspace/bin/getpin.s ./tmp/mockups/usrbinmockup/getpin
./bin/assembler ./src/userspace/bin/setpin.s ./tmp/mockups/usrbinmockup/setpin
./bin/assembler ./src/userspace/bin/reset.s ./tmp/mockups/usrbinmockup/reset
./bin/assembler ./src/userspace/bin/hash.s ./tmp/mockups/usrbinmockup/hash

# Build volumes
./bin/minifsbuilder -fcheader "./tmp/mockups/binmockup" "bin" "./src/kernel"
./bin/minifsbuilder -fcheader "./tmp/mockups/usrbinmockup" "usrbin" "./src/kernel"
./bin/minifsbuilder -fcheader "./tmp/mockups/usrgamesmockup" "usrgames" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/curses" "libcurses" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/pin" "libpin" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/io" "libstdio" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/math" "libstdmath" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/mem" "libstdmem" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/proc" "libstdproc" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/str" "libstdstr" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/std/time" "libstdtime" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/bin/lib/sys" "libsys" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/man/1" "man1" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/man/2" "man2" "./src/kernel"
./bin/minifsbuilder -fcheader "./src/userspace/man/3" "man3" "./src/kernel"

./bin/minifsbuilder --size=1024 -fflatfile "./tmp/mockups/etcmockup" "etc" "./tmp"
./bin/minifsbuilder --size=3072 -fflatfile "./tmp/mockups/homemockup" "home" "./tmp"
cat ./tmp/etc ./tmp/home > ./eeprom
