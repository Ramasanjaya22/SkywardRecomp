#pragma once
// The ZR sword jab (setting zr_attack). In the button-only scheme ZR is the item wheel, so a tap of it is
// free: this turns that tap into the right-stick click, the game's own jab, and leaves a hold to the wheel.
// One frame in, one frame out: the pulse only says which frames carry the click.
#include <stdbool.h>
#include <stdint.h>

enum { ZR_TAP_MS = 300, ZR_PULSE = 3 };  // a press shorter than this is a tap; the click lasts this many polls

typedef struct {
    bool down;       // ZR is held from this press
    uint64_t ms;     // when that press started (SDL_GetTicks)
    uint32_t pulse;  // polls left carrying the click
} ZrTap;

// Feed one poll; true when this poll carries the right-stick click. blocked (R / ZL held, menu up) keeps
// those buttons' own game action and cancels a pulse in flight.
static inline bool zr_tap_step(ZrTap *t, bool zr_now, bool blocked, uint64_t now) {
    if (t->pulse) t->pulse--;
    if (zr_now && !t->down) {
        t->down = true;
        t->ms = now;
    } else if (!zr_now && t->down) {
        t->down = false;
        if (!blocked && now - t->ms <= ZR_TAP_MS) t->pulse = ZR_PULSE;
    }
    return t->pulse != 0 && !blocked;
}
