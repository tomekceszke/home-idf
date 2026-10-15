#pragma once

/*
 * When a value that is read often is worth sending: the first reading, a change of at least `delta` from the last
 * value sent, or `max_gap_s` since the last send. A quiet sensor then costs one message per max_gap_s and a changing
 * one follows its reads. Pure C (no ESP-IDF), host-tested in test/test_deadband.c.
 *
 * max_gap_s is a bound on the sends only when it is a whole number of read periods; with reads every P s, pass
 * (the wanted bound - P) to keep every window of the wanted length with at least one send.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float sent;         // last value sent
    uint32_t sent_s;    // monotonic seconds of that send
    bool has;           // something was sent since the last reset
} hi_deadband_t;

/* True when value should be sent; it then counts as sent. Zero-initialise d before the first call. */
bool hi_deadband_due(hi_deadband_t *d, float value, uint32_t now_s, float delta, uint32_t max_gap_s);

/* The next reading is sent whatever its value (e.g. a sensor that was lost and is back). */
void hi_deadband_reset(hi_deadband_t *d);
