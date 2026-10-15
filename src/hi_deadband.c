#include <math.h>

#include "hi_deadband.h"

bool hi_deadband_due(hi_deadband_t *d, float value, uint32_t now_s, float delta, uint32_t max_gap_s)
{
    // Unsigned difference: a wrapped counter still gives the elapsed time
    if (d->has && fabsf(value - d->sent) < delta && now_s - d->sent_s < max_gap_s) return false;
    d->sent = value;
    d->sent_s = now_s;
    d->has = true;
    return true;
}

void hi_deadband_reset(hi_deadband_t *d)
{
    d->has = false;
}
