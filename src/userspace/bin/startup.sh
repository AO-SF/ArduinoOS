#!/bin/sh

# Mount user's home directory from EEPROM
mount customminifs /dev/eeprom /home

# Done
exit

# Example Code

# Register SD card reader in HW device slot 0 and attempt to mount SD card
hwreg 0 sdcardreader 91 90 # register an SD card reader in slot 0 (power pin D46, slave select pin D47)
hwsdmnt 0 /dev/sd
mount fat /dev/sd /media/sd
