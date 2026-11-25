// The "Keyboard + Mouse" key preset (bind_apply_kbm_preset, runtime/hle/bindings.c): the keyboard slots it sets, by
// SDL_Scancode. PROVISIONAL: a first guess, not yet verified in game;
// if the game turns out to want a different layout, only this table changes. 0 = unbound.
#pragma once
#include <SDL3/SDL_scancode.h>
#include <stdint.h>
#include "input_bind.h"

static const int16_t k_kbm_key[BIND_KEY_COUNT] = {
    [BIND_A] = SDL_SCANCODE_E,           // interact / context
    [BIND_B] = SDL_SCANCODE_LSHIFT,      // sprint
    [BIND_X] = SDL_SCANCODE_SPACE,       // jump
    [BIND_Y] = SDL_SCANCODE_F,           // item action
    [BIND_L] = SDL_SCANCODE_Q,           // free-camera toggle (short tap)
    [BIND_ZR] = SDL_SCANCODE_R,          // item wheel
    [BIND_PLUS] = SDL_SCANCODE_RETURN,   // pause (Esc is taken by the overlay menu: menu.mm opens it before bindings see Esc)
    [BIND_MINUS] = SDL_SCANCODE_M,       // map
    [BIND_DOWN] = SDL_SCANCODE_TAB,      // inventory
    [BIND_LS_UP] = SDL_SCANCODE_W, [BIND_LS_DOWN] = SDL_SCANCODE_S,
    [BIND_LS_LEFT] = SDL_SCANCODE_A, [BIND_LS_RIGHT] = SDL_SCANCODE_D,
    // everything else (stick clicks, R, ZL, d-pad but Down, right stick) stays unbound: the mouse drives those
};
