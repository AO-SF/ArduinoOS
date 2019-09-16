#!/bin/sh

# Unmount user's home directory
unmount /dev/eeprom

# Done
exit

# Example Code

# Deregister SD card read (unmounting SD card volume automatically, if any).
unmount /dev/sd
hwdereg 0
