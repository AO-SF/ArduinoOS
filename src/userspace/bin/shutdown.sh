#!/bin/sh

# Unmount user's home directory
unmount /home

# Done
exit

# Example Code

# Deregister SD card read (unmounting SD card volume automatically, if any).
unmount /media/sd
hwdereg 0
