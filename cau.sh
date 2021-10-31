#!/bin/bash

set -euo pipefail

echo "-- Compiling"
arduino-cli --fqbn arduino:avr:nano:cpu=atmega328old compile
echo
echo "-- Uploading"
arduino-cli --fqbn arduino:avr:nano:cpu=atmega328old upload -p /dev/ttyUSB0

stty -F /dev/ttyUSB0 -hupcl -echo 115200
