#!/bin/sh

# Deregister SD card read (unmounting SD card volume automatically, if any).
unmount /dev/sd
hwdereg 0

# Unmount user's home directory
unmount /dev/eeprom

# Done
exit
