#!/bin/sh

# Mount user's home directory from EEPROM
mount customminifs /dev/eeprom /home

# Register SD card reader in SPI slot 0 and attempt to mount SD card
spireg 0 2 # register an SD card reader in slot 0
spisdmnt 0 /dev/sd

# Done
exit
