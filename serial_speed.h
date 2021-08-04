//
// File shared by 2-3 very different programs, that MUST share information
// about the serial line (USB actually) speed.
//   mapper-devusb.c    daemon to pass on orders to Arduino board
//   transcode.ino      Arduino sketch to (among others things) read from USB
//                      the orders sent by mapper-devusb daemon
//   Also new:
//   rf433trans.ino     Another Arduino sketch doing things similar to
//                      transcode.ino.

// Copyright 2020, 2021 Sébastien Millet

    // Used by mapper-devusb.c
    // Constant is speed_t, to call cfsetospeed
#define SERIAL_SPEED_SPEED_T B115200

    // Used by transcode.ino
    // Constant is a regular integer, to call Serial.begin
#define SERIAL_SPEED_INTEGER 115200

