/*
  rf433trans.ino

  Receives codes from a telecommand and sends codes to other telecommands.
  - Receives from an RF 433 Mhz receiver.
  - Sends with an RF 433 Mhz transmitter.

  Schema:
    'data' of RF433 receiver needs be plugged on PIN 'D2' of Arduino.
    'data' of RF433 transmitter needs be plugged on PIN 'D3' of Arduino.

  Also receives instructions from USB.
*/

/*
  Copyright 2021 Sébastien Millet

  `rf433trans.ino' is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  `rf433trans.ino' is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses>.
*/

#define DEBUG

#define PIN_RFINPUT  2
#define PIN_RFOUT    3
  // Comment the below line if you don't want a LED to show RF transmission is
  // underway.
#define PIN_LED      LED_BUILTIN

#include "RF433any.h"
#include "RF433send.h"
#include "DelayExec.h"
#include "serial_speed.h"

#include <Arduino.h>

extern DelayExec dx;

#ifdef DEBUG

static char serial_printf_buffer[80];

static void serial_printf(const char* fmt, ...)
     __attribute__((format(printf, 1, 2)));

static void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(serial_printf_buffer, sizeof(serial_printf_buffer), fmt, args);
    va_end(args);

    serial_printf_buffer[sizeof(serial_printf_buffer) - 1] = '\0';
    Serial.print(serial_printf_buffer);
}

#else // DEBUG

#define serial_printf(...)

#endif // DEBUG

#define ASSERT_OUTPUT_TO_SERIAL

#define assert(cond) { \
    if (!(cond)) { \
        assert_failed(__LINE__); \
    } \
}

static void assert_failed(int line) {
#ifdef ASSERT_OUTPUT_TO_SERIAL
    Serial.print("\ntest.ino:");
    Serial.print(line);
    Serial.println(": assertion failed, aborted.");
#endif
    while (1)
        ;
}

#define ARRAYSZ(a) (sizeof(a) / sizeof(*a))

#define NOOP_BLINK

#define SIMULATE_BUSY_TX
#define SIMULATE_TX_SEND

  // Input (codes received from Sonoff telecommand)
//#define CODE_IN_BTN_HAUT     0x00b94d24
//#define CODE_IN_BTN_BAS      0x00b94d22

    // Volet salon
const byte sl1_open_code[] =  {0x40, 0xA2, 0xBB, 0xAE};
#define ID_SL1_OPEN           10
const byte sl1_close_code[] = {0x40, 0xA2, 0xBB, 0xAD};
#define ID_SL1_CLOSE          11
    // Volet salle à manger
const byte sl2_open_code[] =  {0x40, 0x03, 0x89, 0x4D};
#define ID_SL2_OPEN           20
const byte sl2_close_code[] = {0x40, 0x03, 0x89, 0x4E};
#define ID_SL2_CLOSE          21
#define ID_SL2_CLOSE_PARTIAL  22
    // Volet chambre
const byte sl3_open_code[] =  {0x40, 0x78, 0x49, 0x5E};
#define ID_SL3_OPEN           30
const byte sl3_close_code[] = {0x40, 0x78, 0x49, 0x5D};
#define ID_SL3_CLOSE          31

#define SLATER_IS_OPEN    0
#define SLATER_IS_CLOSED  1
#define SLATER_DEFAULT    SLATER_IS_OPEN

#define SLATER_WHAT_UNDEF 255
#define SLATER_WHAT_OPEN  0
#define SLATER_WHAT_CLOSE 1

class Slater {
    protected:
        byte status;

        virtual void action_child(byte what) = 0;

    public:
        Slater():status(SLATER_DEFAULT) { }
        virtual ~Slater() { }
        virtual void action(byte what);
};

void Slater::action(byte what) {
    action_child(what);
    status = (what == SLATER_WHAT_OPEN ? SLATER_IS_OPEN : SLATER_IS_CLOSED);
}

#define SLATERADF_LEN        4
class SlaterAdf : public Slater {
    private:
        static RfSend *tx;

        const byte *open_code;
        const byte *close_code;

    protected:
        virtual void action_child(byte what) override;

    public:
        SlaterAdf(byte len, const byte *arg_open_code,
                const byte *arg_close_code);
        virtual ~SlaterAdf() { }
};

RfSend *SlaterAdf::tx = nullptr;

SlaterAdf::SlaterAdf(byte len, const byte *arg_open_code,
        const byte *arg_close_code):
        open_code(arg_open_code),
        close_code(arg_close_code) {
    assert(len == SLATERADF_LEN);

        // tx is a static member, we need create it once.
    if (!tx) {
        tx = rfsend_builder(RfSendEncoding::MANCHESTER, PIN_RFOUT,
                RFSEND_DEFAULT_CONVENTION, 8, nullptr, 5500, 0, 0, 0, 1150, 0,
                0, 0, 0, 6900, 32);
    }

    assert(tx);
}

#ifdef SIMULATE_TX_SEND
int simulate_tx_send(byte len, const byte *data) {
    serial_printf("tx(%d):", len);
    for (byte i = 0; i < len; ++i) {
        serial_printf(" %02X", data[i]);
    }
    serial_printf("\n");
    return 1;
}
#endif

void SlaterAdf::action_child(byte what) {
    assert(what == SLATER_WHAT_OPEN || what == SLATER_WHAT_CLOSE);
    const byte *pcode =
        (what == SLATER_WHAT_OPEN ? open_code : close_code);
#ifdef SIMULATE_TX_SEND
    simulate_tx_send(SLATERADF_LEN, pcode);
#else
    tx->send(SLATERADF_LEN, pcode);
#endif
}

struct code_t {
    byte id;
    Slater *sl;
    byte what;
    unsigned long delayed_action;
    byte delayed_action_id;
};

SlaterAdf *sl1 = new SlaterAdf(ARRAYSZ(sl1_open_code),
        sl1_open_code, sl1_close_code);
SlaterAdf *sl2 = new SlaterAdf(ARRAYSZ(sl2_open_code),
        sl2_open_code, sl2_close_code);
SlaterAdf *sl3 = new SlaterAdf(ARRAYSZ(sl3_open_code),
        sl3_open_code, sl3_close_code);

code_t slater_codes[] = {
    {ID_SL1_OPEN,           sl1, SLATER_WHAT_OPEN,     0, SLATER_WHAT_UNDEF},
    {ID_SL1_CLOSE,          sl1, SLATER_WHAT_CLOSE,    0, SLATER_WHAT_UNDEF},
    {ID_SL2_OPEN,           sl2, SLATER_WHAT_OPEN,     0, SLATER_WHAT_UNDEF},
    {ID_SL2_CLOSE,          sl2, SLATER_WHAT_CLOSE,    0, SLATER_WHAT_UNDEF},
    {ID_SL2_CLOSE_PARTIAL,  sl2, SLATER_WHAT_OPEN, 1650, SLATER_WHAT_CLOSE}, // FIXME
    {ID_SL3_OPEN,           sl3, SLATER_WHAT_OPEN,     0, SLATER_WHAT_UNDEF},
    {ID_SL3_CLOSE,          sl3, SLATER_WHAT_CLOSE,    0, SLATER_WHAT_UNDEF},
};

Track track(PIN_RFINPUT);
byte dummy;

byte tx_is_busy = false;

bool tx_set_busy() {
#ifdef SIMULATE_BUSY_TX
    static byte counter = 0;
#endif

    cli();

#ifdef SIMULATE_BUSY_TX
    if ((++counter % 4) || tx_is_busy) {
#else
    if (tx_is_busy) {
#endif
        sei();
        return false;
    }
    tx_is_busy = true;
    sei();
    return true;
}

void tx_clear_busy() {
    assert(tx_is_busy);
    cli();
    tx_is_busy = false;
    sei();
}

void tx_by_id(void *data) {
    byte id = (byte *)data - &dummy;
    int idx = -1;
    for (unsigned int i = 0; i < ARRAYSZ(slater_codes); ++i) {
        if (slater_codes[i].id == id) {
            idx = i;
                // Uncomment the below?
                // I prefer to leave it commented. Execution time is then
                // constant (and therefore, predictable).
//            break;
        }
    }
    if (idx < 0) {
        serial_printf("tx_by_id(): unknown id %d\n", id);
        return;
    }

    if (!tx_set_busy()) {
        dx.set_task(100, tx_by_id, data, false);
        serial_printf("Exec deferred by 100ms\n");
    } else {
        code_t *psc = &slater_codes[idx];
        (psc->sl)->action(psc->what);
        serial_printf("Exec done\n");
        tx_clear_busy();
    }
}

void rf_recv_callback(void *data) {
    int n = (byte *)data - &dummy;
    serial_printf("call of rf_recv_callback(): n = %d\n", (int)n);
    if (n == 1) {
//        tx();
    } else if (n == 2) {
//        tx();
    }
}

void turn_led_on() {
#ifdef PIN_LED
    digitalWrite(PIN_LED, HIGH);
#endif
}

void turn_led_off() {
#ifdef PIN_LED
    digitalWrite(PIN_LED, LOW);
#endif
}

void setup() {
    Serial.begin(SERIAL_SPEED_INTEGER);
    Serial.print("Start\n");

    pinMode(PIN_RFINPUT, INPUT);
    turn_led_off();

//    tx_flo = rfsend_builder(RfSendEncoding::TRIBIT_INVERTED, PIN_RFOUT,
//            RFSEND_DEFAULT_CONVENTION, 8, nullptr, 24000, 0, 0, 650, 650, 1300,
//            0, 0, 0, 24000, 12);

    track.setopt_wait_free_433_before_calling_callbacks(true);
    track.register_callback(RF433ANY_ID_TRIBIT,
            new BitVector(32, 4, 0xb9, 0x35, 0x6d, 0x00),
            (void *)(&dummy + 1), rf_recv_callback, 2000);
    track.register_callback(RF433ANY_ID_TRIBIT,
            new BitVector(32, 4, 0xb5, 0x35, 0x6d, 0x00),
            (void *)(&dummy + 2), rf_recv_callback, 2000);

    dx.activate();
}

void noop() {
#ifdef NOOP_BLINK
    for (int i = 0; i < 2; ++i) {
        turn_led_on();
        delay(125);
        turn_led_off();
        delay(125);
    }
#endif // NOOP_BLINK
}

//
// SerialLine
//
// Manages USB input as lines.
//
// Interest = non blocking I/O. Serial.readString() works with timeout and a
// null timeout is not well documented (meaning: even if zeroing timeout leads
// to non-blocking I/O, I'm not sure it'll consistently and robustly *always*
// behave this way).
class SerialLine {
    private:
        char buf[43]; // 40-character strings (then CR+LF then NULL-terminating)
        size_t head;
        bool got_a_line;
        void reset();

    public:
        SerialLine();

        static const size_t buf_len;

        void do_events();
        bool is_line_available();
        bool get_line(char *s, size_t len);
        void split_s_into_funcname_and_int(char *s, char **func_name,
                int *val) const;
};
const size_t SerialLine::buf_len = sizeof(SerialLine::buf);

SerialLine::SerialLine():head(0),got_a_line(false) { };

void SerialLine::do_events() {
    if (got_a_line)
        return;
    if (!Serial.available())
        return;

    int b;
    do {
        b = Serial.read();
        if (b == -1)
            break;
        buf[head++] = (char)b;
    } while (head < buf_len - 1 && b != '\n' && Serial.available());

    if (head < buf_len - 1 && b != '\n')
        return;

    buf[head] = '\0';

        // Remove trailing cr and/or nl
        // FIXME?
        //   WON'T WORK WITH MAC NEWLINES!
        //   (SEE ABOVE: NO STOP IF ONLY CR ENCOUNTERED)
    if (head >= 1 && buf[head - 1] == '\n')
        buf[--head] = '\0';
    if (head >= 1 && buf[head - 1] == '\r')
        buf[--head] = '\0';
    got_a_line = true;
}

bool SerialLine::is_line_available() {
    do_events();
    return got_a_line;
}

void SerialLine::reset() {
    head = 0;
    got_a_line = false;
}

// Get USB input as a simple line, copied in caller buffer.
// A 'line' is a set of non-null characters followed by 'new line', 'new line'
// being either as per Unix or Windows convention, see below.
// Returns true if a copy was done (there was a line available), false if not
// (in which case, s is not updated).
// The terminating newline character (or 2-character CR-LF sequence) is NOT part
// of the string given to the caller.
// If the line length is above the buffer size (SerialLine::buf_len), then it'll
// be cut into smaller pieces.
// Because of the way the received buffer is parsed, and when using CR-LF as
// end-of-line marker (default even under Linux), it can result in a empty
// string seen after a first string with a length close to the limit.
//
// About new lines:
// - Works fine with Unix new lines (\n), tested
// - Supposed to work fine with Windows new lines (\r\n), NOT TESTED
// - WON'T WORK WITH MAC NEW LINES (\r)
bool SerialLine::get_line(char *s, size_t len) {
    do_events();
    if (!got_a_line)
        return false;
    snprintf(s, len, buf);
    reset();
    return true;
}

// Take a string and splits it into 2 parts, one for the function name, one for
// the argument (an integer).
// If the function is called without argument, *val is set to 0.
//
// IMPORTANT
//   THE PROVIDED STRING, s, IS ALTERED BY THE OPERATION.
void SerialLine::split_s_into_funcname_and_int(char *s, char **func_name,
        int *val) const {
    char *arg = nullptr;
    size_t h = 0;
    *func_name = s;
    while (s[h] != '(' && s[h] != '\0')
        h++;
    if (s[h] == '(') {
        char *open_parenthesis = s + h;
        arg = s + h + 1;
        while (s[h] != ')' && s[h] != '\0')
            h++;
        if (s[h] == ')') {
            if (h < buf_len - 1 && s[h + 1] == '\0') {
                *open_parenthesis = '\0';
                s[h] = '\0';
            } else {
                    // Trailing characters after closing parenthesis -> no
                    // arguments.
                arg = nullptr;
            }
        } else {
                // No closing parenthesis -> no arguments
            arg = nullptr;
        }
    } else {
        arg = nullptr;
    }
    *val = 0;
    if (arg) {
        *val = atoi(arg);
    }
}

SerialLine sl;
char buffer[SerialLine::buf_len];

void manage_serial_line() {
    if (sl.get_line(buffer, sizeof(buffer))) {
        char *func_name;
        int val;
        sl.split_s_into_funcname_and_int(buffer, &func_name, &val);

        serial_printf("<<< [USB]: received %s(%d)\n", func_name, val);
        if (!strcmp(func_name, "tx")) {
            tx_by_id((void *)(&dummy + val));
        } else if (!strcmp(func_name, "noop")) {
            noop();
        } else if (!strcmp(func_name, "")) {
             // Do nothing if empty instruction
             // Alternative: treat as an error?
        } else {
                // Unknown function: we blink 4 times on internal LED
            for (int i = 0; i < 4; ++i) {
                turn_led_on();
                delay(125);
                turn_led_off();
                delay(125);
            }
        }
    }
}

void loop() {
    track.treset();
    while (!track.do_events()) {
        manage_serial_line();
        delay(1);
    }
}

// vim: ts=4:sw=4:et:tw=80
