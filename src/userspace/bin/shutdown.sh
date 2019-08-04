#!/bin/sh

# Deregister SD card read (unmounting SD card volume automatically, if any).
spidereg 0

# Unmount user's home directory
unmount /dev/eeprom

# Done
exit
