# Overview
A kernel/operating system designed to run on Arduino boards (the Uno and Mega in particular), while also proving a wrapper to run on a standard PC.

The kernel provides a basic virtual filesystem, with directories such as ``/bin``, ``/etc`` and ``/tmp``, and can run processes (using preemptive multitasking) written in a custom machine code/bytecode format. The Arduino version also maps e.g. the USB Serial connection to ``/dev/ttyS0`` and the board's pins to ``/dev/pinX``, while the PC wrapper maps stdin/out to ``/dev/ttyS0`` and provides virtual ``/dev/pinX`` files.

The userspace environment consists of various utilities, such as: ``sh`` - with builtins such as ``exit`` and ``cd``, ``mount``, ``kill`` and ``cat``, along with a startup script in ``/etc/startup`` mounting an example home directory (stored in EEPROM). The example home directory contains a few extra example programs, in particular a 2D game ``sokoban``. In addition, if there is space (so in the PC wrapper and on the Mega usually), other read-only directories exist, such as ``/lib`` with assembly source files, and ``/man`` with man-pages split into sections

There are also a few tools (which run on a standard PC, not the Arduino) such as an assembler, a disassembler and an emulator. A compiler is also being developed at [AOSCompiler](https://github.com/DanielWhite94/AOSCompiler).

See the wiki Documentation page for more information.

# Compiling and Building
Run make from the root directory.
This will:

* compile initial tools - the assembler, disassembler and emulator
* run the builder makefile, which will:
	* compile the builder
	* create all mockup directories by copying files and assembling all userspace programs
	* generate C source files for the kenrel, with data representing read-only volumes containing said mockup files
* compiles the kernel (which requies the the generated progmem* files from the builder)

# Running
All compiled tools and the kernel are placed in the ``bin`` directory after compiling. The assembler and disassembler work as one would expect. The emulator takes compiled machine code and runs it on a standard PC - although it is far from a full virtual environment (most syscalls are not implemented, for example). The kernel takes no arguments, and boots into a shell (sh.s) via init (init.s). From there standard commands such as ``cd`` and ``ls`` can be used, and programs on the file system can be executed.

# License
Copyright (C) 2018 Daniel White

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