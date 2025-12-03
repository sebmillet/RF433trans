rf.register_callback(telecommand_otio_up, 2000,
        new BitVector(32, 4, 0xFF, 0xFF, 0xFF, 0xFF));
rf.register_callback(telecommand_otio_down, 2000,
        new BitVector(32, 4, 0xFF, 0xFF, 0xFF, 0xFF));
