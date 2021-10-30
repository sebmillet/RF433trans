#!/bin/bash

set -euo pipefail

arduino-cli --fqbn arduino:avr:nano:cpu=atmega328old compile
arduino-cli --fqbn arduino:avr:nano:cpu=atmega328old upload -p /dev/ttyUSB0

stty -F /dev/ttyUSB0 -hupcl -echo 115200
