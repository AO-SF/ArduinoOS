# Overview
A kernel/operating system designed to run on Arduino boards (the Mega 2560 in particular), while also proving software emulation to allow running on a standard PC, along with tools such as an assembler to write new programs. The basic idea is that these boards are perfect for this application because they have some persistent memory in the form of EEPROM (which can act as a hard drive) as well as a relatively large amount of read-only memory in the form of flash (which can store static data such as essential binaries and man pages). Plus they can easily be expanded by adding hardware such as SD cards or a WiFi chip.

The kernel provides a basic virtual filesystem, with directories such as ``/bin``, ``/etc`` and ``/tmp``, and can run processes (using preemptive multitasking) written in a custom machine code/bytecode format. The Arduino version also maps e.g. the USB Serial connection to ``/dev/ttyS0`` and the board's pins to ``/dev/pinX``, while the PC version maps stdin/out to ``/dev/ttyS0`` and provides virtual ``/dev/pinX`` files.

The userspace environment consists of various utilities, such as: ``sh`` - with builtins such as ``exit`` and ``cd`` - ``mount``, ``kill`` and ``cat``, along with a startup script in ``/etc/startup`` mounting the user's home directory (stored in EEPROM). The example home directory contains a few extra example programs, in particular a 2D game ``sokoban``. In addition, if there is space on the board, other read-only directories exist, such as ``/lib`` with assembly source files, and ``/man`` with man-pages split into sections

There are also a few tools (which run on a standard PC, not the Arduino) such as an assembler, a disassembler and an emulator. A compiler is also being developed at [AOSCompiler](https://github.com/AO-SF/AOSCompiler).

# Documentation
See the [wiki](https://github.com/AO-SF/ArduinoOS/wiki/Home).

# Compiling and Building
Run either ``make arduino`` or ``make pc`` from the root directory.
This will:

* compile initial tools - the assembler, disassembler, emulator and a minifs volume creator
* run the builder script, which will:
	* create a local temporary version of the file system by copying files and assembling all userspace programs
	* generate C source files for the kernel, with data representing read-only volumes containing said file system
	* generate a local file representing the EEPROM data with ``/etc`` and ``/home`` in it, ready to be flashed to an Arduino
* compile the kernel (which requies the the generated progmem* files from the builder)

# Running
## Tools and Emulated Kernel
All compiled tools and the kernel are placed in the ``bin`` directory after compiling. The assembler and disassembler work as one would expect. The emulator takes compiled machine code and runs it on a standard PC - although it is far from a full virtual environment (most syscalls are not implemented, for example).

The kernel takes no arguments, and boots into a shell (sh.s) via init (init.s). From there standard commands such as ``cd`` and ``ls`` can be used, and programs on the file system can be executed. Note: The local EEPROM file - which is generated during a build - is stored in the project root so run the kernel from there as ``./bin/kernel`` so it can find it. Logs are written to ``kernel.log``.

## Arduino
Note: Currently only the Arduino Mega 2560 is supported.

### Uploading
After running ``make arduino``, the hex file ``./bin/kernel.hex`` is produced, which needs flashing to the Arduino with ``make upload``. Note: if your device is not at ``/dev/ttyACM0`` then the root makefile will need modifying. This will also flash the local EEPROM file to setup things such as the user's home directory.

### Interfacing
Kernel logging and stdin/stout uses the Mega's USB serial, baud rate 9600 and a carriage return plus newline ``'\r\n'`` pair for line endings. It accepts either ``\r`` OR ``\n``, but both will result in a double 'return'.

Example: ``screen /dev/ttyACM0 9600``

# Examples
```
/bin$ cd
/home$ ls
fib bomb tree blinkfast blink test.sh
/home$ fib
0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946, 17711, 28657, 46368
/home$ cat test.sh
#!/bin/sh
echo moving to home...
cd
echo timing ls...
time ls
echo exiting...
exit
/home$ test.sh
moving to home...
timing ls...
fib bomb tree blinkfast blink test.sh
took: 0s
exiting...
/home$ ls /dev
eeprom full null ttyS0 urandom zero pin12 pin13 pin14 pin15 pin35 pin36 pin37 pin53 pin59 pin60 pin61 pin62
/home$
/home$ sleep 100 &
/home$ ps
  PID  %CPU   RAM    STATE COMMAND
00000 00000 00094  waiting /bin/init
00001 00033 00462  waiting /bin/sh
00002 00036 00130  waiting /bin/sleep
00003 00030 00221   active /usr/bin/ps
/home$ kill 2
/home$ ps
  PID  %CPU   RAM    STATE COMMAND
00000 00000 00094  waiting /bin/init
00001 00024 00462  waiting /bin/sh
00002 00075 00221   active /usr/bin/ps
/home$ shutdown
```

![Screenshot of Arduino Game](https://raw.githubusercontent.com/AO-SF/ArduinoOS/master/screenshots/game.png)

# License
Copyright (C) 2018-2019 Daniel White

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
