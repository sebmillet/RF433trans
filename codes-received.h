rf.register_callback(telecommand_otio_up, 2000,
        new BitVector(32, 4, 0x92, 0x93, 0x94, 0x95));
rf.register_callback(telecommand_otio_down, 2000,
        new BitVector(32, 4, 0x98, 0x99, 0x9A, 0x9B));
