  // Input \(0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1\)

    // Volet salon
const byte sl1_open_code[] =  {0xA2, 0xA3, 0xA4, 0xA5};
#define ID_SL1_OPEN           10
const byte sl1_close_code[] = {0xA6, 0xA7, 0xA8, 0xA9};
#define ID_SL1_CLOSE          15

    // Volet salle à manger
const byte sl2_open_code[] =  {0xAA, 0xAB, 0xAC, 0xAD};
#define ID_SL2_OPEN           20
#define ID_SL2_STOP           21
const byte sl2_close_code[] = {0xAE, 0xAF, 0xB0, 0xB1};
#define ID_SL2_CLOSE          25
#define ID_SL2_CLOSE_PARTIAL  26

    // Volet chambre
const byte sl3_open_code[] =  {0xB2, 0xB3, 0xB4, 0xB5};
#define ID_SL3_OPEN           30
const byte sl3_close_code[] = {0xB6, 0xB7, 0xB8, 0xB9};
#define ID_SL3_CLOSE          35

    // Volet salle à manger numéro (latéral)
const byte sl4_open_code[] =  {0xBA, 0xBB};
#define ID_SL4_OPEN           40
const byte sl4_close_code[] = {0xBC, 0xBD};
#define ID_SL4_CLOSE          45
const byte sl4_stop_code[] = {0xBE, 0xBF};
#define ID_SL4_STOP           47

