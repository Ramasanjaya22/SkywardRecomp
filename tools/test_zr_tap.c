#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "hle/zr_tap.h"

int main(void) {
    // A short press and release pulses the right-stick click for ZR_PULSE frames.
    ZrTap t = {0};
    assert(!zr_tap_step(&t, true, false, 0));    // press
    assert(zr_tap_step(&t, false, false, 10));   // release: pulse starts
    assert(zr_tap_step(&t, false, false, 20));
    assert(zr_tap_step(&t, false, false, 30));
    assert(!zr_tap_step(&t, false, false, 40));  // pulse over

    // A hold is the item wheel, never a jab.
    ZrTap h = {0};
    assert(!zr_tap_step(&h, true, false, 0));
    assert(!zr_tap_step(&h, true, false, 100));
    assert(!zr_tap_step(&h, true, false, 400));
    assert(!zr_tap_step(&h, false, false, 410));

    // R or ZL held: the stick / aim keeps its game action, so no jab.
    ZrTap b = {0};
    assert(!zr_tap_step(&b, true, false, 0));
    assert(!zr_tap_step(&b, false, true, 10));
    assert(!zr_tap_step(&b, false, true, 20));

    // A pulse already running is cut short by those buttons.
    ZrTap c = {0};
    assert(!zr_tap_step(&c, true, false, 0));
    assert(zr_tap_step(&c, false, false, 10));
    assert(!zr_tap_step(&c, false, true, 20));
    return 0;
}
