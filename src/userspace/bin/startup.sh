#!/bin/sh

# Mount user's home directory from EEPROM
mount customminifs /dev/eeprom /home

# Done
exit

# Example Code

# Register SD card reader in HW device slot 0 and attempt to mount SD card
hwreg 0 sdcardreader # register an SD card reader in slot 0
hwsdmnt 0 /dev/sd
mount customminifs /dev/sd /media/sd
