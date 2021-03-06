/*
  RF433trans.ino

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

  `RF433trans.ino' is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  `RF433trans.ino' is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses>.
*/

//#define DEBUG

#define PIN_RFINPUT  2
#define INT_RFINPUT  0

#define PIN_RFOUT    3
  // Comment the below line if you don't want a LED to show RF transmission is
  // underway.
#define PIN_LED      LED_BUILTIN

//#define SIMULATE_BUSY_TX
//#define SIMULATE_TX_SEND

    // Should we blink when a noop() instruction is received on the serial line?
#define NOOP_BLINK

    // When we receive a signal from OTIO, we wait a bit before executing
    // subsequent orders. Unit is milli-seconds.
#define OTIO_DELAY_TO_EXECUTE_AFTER_RECEPTION 750

    // When we receive a signal from FLO/R, we wait a bit before executing
    // subsequent orders. Unit is milli-seconds.
#define FLOR_DELAY_TO_EXECUTE_AFTER_RECEPTION 750

    // If we got to defer signal sending because TX is already busy, how long
    // shall we wait? (in milli-seconds)
#define DELAY_WHEN_TX_IS_BUSY 100LU

#include "RF433recv.h"
#include "RF433send.h"
#include "DelayExec.h"
#include "serial_speed.h"

#include <Arduino.h>

extern DelayExec dx;
RF_manager rf(PIN_RFINPUT, INT_RFINPUT);

byte dummy;

void tx_by_id(void *data);

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
    Serial.print("\nRF433trans.ino:");
    Serial.print(line);
    Serial.println(": assertion failed, aborted.");
#endif
    while (1)
        ;
}

#define ARRAYSZ(a) (sizeof(a) / sizeof(*a))


// * ****** *
// * SLATER *
// * ****** *

#define SLATER_IS_OPEN    0
#define SLATER_IS_CLOSED  1
#define SLATER_DEFAULT    SLATER_IS_OPEN

#define SLATER_WHAT_UNDEF         255
#define SLATER_WHAT_OPEN            0
#define SLATER_WHAT_CLOSE           1
#define SLATER_WHAT_CLOSE_PARTIAL   2
#define SLATER_WHAT_STOP            3

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
    dx.inactivate();
    rf.inactivate_interrupts_handler();
    action_child(what);
    rf.activate_interrupts_handler();
    dx.activate();
    status = (what == SLATER_WHAT_OPEN ? SLATER_IS_OPEN : SLATER_IS_CLOSED);
}

#ifdef SIMULATE_TX_SEND
static int simulate_tx_send(byte len, const byte *data) {
    serial_printf("tx(%d):", len);
    for (byte i = 0; i < len; ++i) {
        serial_printf(" %02X", data[i]);
    }
    serial_printf("\ntx(%d)\n", len);

    return 1;
}
#endif


// * ********* *
// * SLATERADF *
// * ********* *

#define SLATERADF_LEN                   4
    // SLATERADF_MAX_DELAY_PARTIAL corresponds to a delay of 30 seconds. The
    // below means, if a partial close order was sent since more than 30 seconds
    // ago, then, ignore it.
    // The second constant (SLATERADF_MIN_DELAY_PARTIAL corresponds to 2
    // seconds) is also necessary to ignore two commands received in a short
    // period of time, most often, due to Radio-Frequency signal duplication.
#define SLATERADF_MAX_DELAY_PARTIAL 30000
#define SLATERADF_MIN_DELAY_PARTIAL  2000
class SlaterAdf : public Slater {
    private:
        static RfSend *tx;

        const byte *open_code;
        const byte *close_code;

        unsigned long last_millis;
        byte last_what;

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
        close_code(arg_close_code),
        last_millis(0),
        last_what(SLATER_WHAT_UNDEF) {
    assert(len == SLATERADF_LEN);

        // tx is a static member, we need create it once.
    if (!tx) {
        tx = rfsend_builder(RfSendEncoding::MANCHESTER, PIN_RFOUT,
                RFSEND_DEFAULT_CONVENTION, 8, nullptr, 20000, 0, 0, 0, 1150, 0,
                0, 0, 0, 5500, 32);
    }

    assert(tx);
}

void SlaterAdf::action_child(byte what) {
    const byte *pcode =
        ((what == SLATER_WHAT_OPEN || what == SLATER_WHAT_STOP) ?
         open_code : close_code);

    unsigned long t = millis();
    unsigned long d = t - last_millis;
    last_millis = t;

    serial_printf("t = %lu, d = %lu, last_what = %d, what = %d\n",
            t, d, last_what, what);

    if (last_what != SLATER_WHAT_UNDEF && d >= SLATERADF_MIN_DELAY_PARTIAL
            && d <= SLATERADF_MAX_DELAY_PARTIAL) {
        if (what == SLATER_WHAT_STOP) {
            if (last_what != SLATER_WHAT_CLOSE_PARTIAL)
                what = SLATER_WHAT_UNDEF;
        } else if (what == SLATER_WHAT_CLOSE_PARTIAL) {
            if (last_what == SLATER_WHAT_CLOSE
                    || last_what == SLATER_WHAT_CLOSE_PARTIAL) {
                what = SLATER_WHAT_UNDEF;
            }
        }
    } else {
        if (what == SLATER_WHAT_STOP) {
            what = SLATER_WHAT_UNDEF;
        }
    }

    last_what = what;

    serial_printf("what = %d\n", what);

    if (what == SLATER_WHAT_UNDEF)
        return;

#ifdef SIMULATE_TX_SEND
    simulate_tx_send(SLATERADF_LEN, pcode);
#else
    byte n = tx->send(SLATERADF_LEN, pcode);
    (void)n; // To turn off warning when debugging is off
    serial_printf("Sent %d time(s)\n", n);
#endif
}


// * ********** *
// * SLATERMETA *
// * ********** *

struct id_sched_t {
    unsigned long delay;
    byte id;
};

class SlaterMeta : public Slater {
    private:
        const byte n;
        const id_sched_t *sched;

    protected:
        virtual void action_child(byte what) override;

    public:
        SlaterMeta(byte arg_n, const id_sched_t *arg_sched):
            n(arg_n),
            sched(arg_sched) { }
        virtual ~SlaterMeta() { }
};

void SlaterMeta::action_child(byte what) {
    dx.delete_all_tasks();
    unsigned long cumul_delay = 0;
    for (byte i = 0; i < n; ++i) {
        const id_sched_t *psched = &sched[i];
        void *data = &dummy + psched->id;
        cumul_delay += psched->delay;
        dx.set_task(cumul_delay, tx_by_id, data, false);
    }
}


// * ********* *
// * SLATERFLO *
// * ********* *

#define SLATERFLO_LEN 2
class SlaterFlo : public Slater {
    private:
        static RfSend *tx;

        const byte *open_code;
        const byte *close_code;
        const byte *stop_code;

    protected:
        virtual void action_child(byte what) override;

    public:
        SlaterFlo(byte len, const byte *arg_open_code,
                const byte *arg_close_code, const byte *arg_stop_code);
        virtual ~SlaterFlo() { }
};

RfSend *SlaterFlo::tx = nullptr;

SlaterFlo::SlaterFlo(byte len, const byte *arg_open_code,
        const byte *arg_close_code, const byte *arg_stop_code):
        open_code(arg_open_code),
        close_code(arg_close_code),
        stop_code(arg_stop_code) {
    assert(len == SLATERFLO_LEN);

        // tx is a static member, we need create it once.
    if (!tx) {
        tx = rfsend_builder(RfSendEncoding::TRIBIT_INVERTED, PIN_RFOUT,
                RFSEND_DEFAULT_CONVENTION, 8, nullptr, 24000, 0, 0, 650, 650,
                1300, 0, 0, 0, 24000, 12);
    }

    assert(tx);
}

void SlaterFlo::action_child(byte what) {
    const byte *pcode;
    if (what == SLATER_WHAT_OPEN)
        pcode = open_code;
    else if (what == SLATER_WHAT_CLOSE)
        pcode = close_code;
    else if (what == SLATER_WHAT_STOP)
        pcode = stop_code;
    else
        assert(false);

    serial_printf("SlaterFlo::action_child: what = %d\n", what);

#ifdef SIMULATE_TX_SEND
    simulate_tx_send(SLATERFLO_LEN, pcode);
#else
    byte n = tx->send(SLATERFLO_LEN, pcode);
    (void)n; // To turn off warning when debugging is off
    serial_printf("Sent %d time(s)\n", n);
#endif
}


// * **************** *
// * MY DEVICES CODES *
// * **************** *

struct code_t {
    byte id;
    Slater *sl;
    byte what;
    unsigned long delayed_action;
    byte delayed_action_id;
};

#include "codes-sent.h"

#define ID_SLA_OPEN          100
#define ID_SLA_CLOSE         101

SlaterAdf *sl1 = new SlaterAdf(ARRAYSZ(sl1_open_code),
        sl1_open_code, sl1_close_code);
SlaterAdf *sl2 = new SlaterAdf(ARRAYSZ(sl2_open_code),
        sl2_open_code, sl2_close_code);
SlaterAdf *sl3 = new SlaterAdf(ARRAYSZ(sl3_open_code),
        sl3_open_code, sl3_close_code);

SlaterFlo *sl4 = new SlaterFlo(ARRAYSZ(sl4_open_code),
        sl4_open_code, sl4_close_code, sl4_stop_code);

const id_sched_t all_open[] = {
       0, ID_SL1_OPEN,
    2000, ID_SL2_OPEN,
    2000, ID_SL3_OPEN,
    2000, ID_SL4_OPEN
};
SlaterMeta *sla_open = new SlaterMeta(ARRAYSZ(all_open), all_open);

const id_sched_t all_close[] = {
       0, ID_SL1_CLOSE,
    2000, ID_SL2_CLOSE,
    2000, ID_SL3_CLOSE,
    2000, ID_SL4_CLOSE
};
SlaterMeta *sla_close = new SlaterMeta(ARRAYSZ(all_close), all_close);

code_t slater_codes[] = {
    {ID_SL1_OPEN,          sl1,       SLATER_WHAT_OPEN,  0, SLATER_WHAT_UNDEF},
    {ID_SL1_CLOSE,         sl1,       SLATER_WHAT_CLOSE, 0, SLATER_WHAT_UNDEF},
    {ID_SL2_OPEN,          sl2,       SLATER_WHAT_OPEN,  0, SLATER_WHAT_UNDEF},
    {ID_SL2_STOP,          sl2,       SLATER_WHAT_STOP,  0, SLATER_WHAT_UNDEF},
    {ID_SL2_CLOSE,         sl2,       SLATER_WHAT_CLOSE, 0, SLATER_WHAT_UNDEF},
    {ID_SL2_CLOSE_PARTIAL, sl2,       SLATER_WHAT_CLOSE_PARTIAL,
        16500, ID_SL2_STOP},
    {ID_SL3_OPEN,          sl3,       SLATER_WHAT_OPEN,  0, SLATER_WHAT_UNDEF},
    {ID_SL3_CLOSE,         sl3,       SLATER_WHAT_CLOSE, 0, SLATER_WHAT_UNDEF},

    {ID_SL4_OPEN,          sl4,       SLATER_WHAT_OPEN,  0, SLATER_WHAT_UNDEF},
    {ID_SL4_CLOSE,         sl4,       SLATER_WHAT_CLOSE, 0, SLATER_WHAT_UNDEF},
    {ID_SL4_STOP,          sl4,       SLATER_WHAT_STOP,  0, SLATER_WHAT_UNDEF},

    {ID_SLA_OPEN,          sla_open,  SLATER_WHAT_UNDEF, 0, SLATER_WHAT_UNDEF},
    {ID_SLA_CLOSE,         sla_close, SLATER_WHAT_UNDEF, 0, SLATER_WHAT_UNDEF}
};

void telecommand_otio_up(const BitVector *recorded) {
    serial_printf("call of telecommand_otio_up()\n");

    void *data = &dummy + ID_SLA_OPEN;
    dx.set_task(OTIO_DELAY_TO_EXECUTE_AFTER_RECEPTION, tx_by_id, data, false);
}

void telecommand_otio_down(const BitVector *recorded) {
    serial_printf("call of telecommand_otio_down()\n");

    void *data = &dummy + ID_SLA_CLOSE;
    dx.set_task(OTIO_DELAY_TO_EXECUTE_AFTER_RECEPTION, tx_by_id, data, false);
}

void telecommand_flor_up() {
    serial_printf("call of telecommand_flor_up()\n");

    void *data = &dummy + ID_SL4_OPEN;
    dx.set_task(FLOR_DELAY_TO_EXECUTE_AFTER_RECEPTION, tx_by_id, data, false);
}

void telecommand_flor_down() {
    serial_printf("call of telecommand_flor_down()\n");

    void *data = &dummy + ID_SL4_CLOSE;
    dx.set_task(FLOR_DELAY_TO_EXECUTE_AFTER_RECEPTION, tx_by_id, data, false);
}

void callback_telecommand_flor_any(const BitVector *recorded) {

        // Defensive programming
        // register_Receiver() sets the number of bits to 72, so we should
        // always have 72 bits when entering here.
    if (recorded->get_nb_bits() != 72)
        return;

        // See 05_rollingcode.ino of RF433recv library to see an explanation of
        // why we do these checks.
    byte first_half_byte = (recorded->get_nth_byte(8) & 0xF0) >> 4;
    byte my_eigth = (recorded->get_nth_byte(2) & 0x0F);

    if (my_eigth != 8)
        return;
    if (first_half_byte == 1)
        telecommand_flor_up();
    else if (first_half_byte == 2)
        telecommand_flor_down();
}

// * **** *
// * CODE *
// * **** *

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
        dx.set_task(DELAY_WHEN_TX_IS_BUSY, tx_by_id, data, false);
        serial_printf("Exec deferred by %lums\n", DELAY_WHEN_TX_IS_BUSY);
    } else {
        code_t *psc = &slater_codes[idx];
        (psc->sl)->action(psc->what);

        serial_printf("id: %d, idx: %d: exec done\n", id, idx);
        tx_clear_busy();

        if (psc->delayed_action) {
            void *deferred_data = &dummy + psc->delayed_action_id;
            dx.set_task(psc->delayed_action, tx_by_id, deferred_data, false);
            serial_printf("id: %d: delay: %lu: exec deferred\n",
                    id, psc->delayed_action);
        }

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

void setup() {
    Serial.begin(SERIAL_SPEED_INTEGER);
    serial_printf("Start\n");

    pinMode(PIN_RFINPUT, INPUT);
    turn_led_off();

        // OTIO
    rf.register_Receiver(RFMOD_TRIBIT, 6976, 0, 0, 0, 562, 1258, 0, 0, 528,
            6996, 32);
    // Setup callbacks on rf object, for certain codes received
    // Is put in a separate file so that it is easy to switch neutral/real codes
    // using different files, thus hiding real codes when committing on github.
    // Real codes are typically found in the 'local' folder.
#include "codes-received.h"

    rf.register_Receiver(RFMOD_TRIBIT, 18000, 1450, 1450, 0, 450, 900, 0, 0,
            1400, 18000, 72, callback_telecommand_flor_any, 2000);

    rf.activate_interrupts_handler();
    dx.activate();
}

void loop() {
    rf.do_events();
    manage_serial_line();
    delay(1);
}

// vim: ts=4:sw=4:et:tw=80
