#!/bin/sh
# shutdown script - called by init after all other processes have finished/been killed
unmount /dev/eeprom
exit
