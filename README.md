Transcode
=========

Receives a code from a telecommand and sends it to a set of others.

- Receive from an RF 433 Mhz receiver.
- Send with an RF 433 Mhz transmitter.

Also can receive instructions from USB.

Schema:

- 'data' of RF433 receiver needs be plugged on PIN 'D2' of Arduino.
- 'data' of RF433 transmitter needs be plugged on PIN 'D3' of Arduino.

serial_speed.h is hard-linked with mapper-devusb project, as this one and
mapper-devusb must share it.


Usage
-----

Requires to install 2 libraries in the Arduino environment:

1. RF433send, available here: https://github.com/sebmillet/rf433send

2. RF433recv, available here: https://github.com/sebmillet/RF433recv

3. DelayExec, available here: https://github.com/sebmillet/DelayExec

