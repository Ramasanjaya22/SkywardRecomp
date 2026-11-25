// "Keyboard + Mouse" preset table (runtime/kbm_preset.h): no clashes, WASD / Q as specified, nothing on the overlay's keys.
#include <SDL3/SDL_scancode.h>
#include "../runtime/kbm_preset.h"
#undef NDEBUG
#include <assert.h>

int main(void) {
    for (int i = 0; i < BIND_KEY_COUNT; i++) {
        int sc = k_kbm_key[i];
        assert(sc >= 0 && sc < SDL_SCANCODE_COUNT);
        if (!sc) continue;
        assert(sc != SDL_SCANCODE_F1 && sc != SDL_SCANCODE_F9 && sc != SDL_SCANCODE_F11);  // menu, frame dump, fullscreen
        for (int j = i + 1; j < BIND_KEY_COUNT; j++) assert(k_kbm_key[j] != sc);           // no two slots share a key
        if (sc == SDL_SCANCODE_Q) assert(i == BIND_L);
    }
    assert(k_kbm_key[BIND_LS_UP] == SDL_SCANCODE_W && k_kbm_key[BIND_LS_DOWN] == SDL_SCANCODE_S);
    assert(k_kbm_key[BIND_LS_LEFT] == SDL_SCANCODE_A && k_kbm_key[BIND_LS_RIGHT] == SDL_SCANCODE_D);
    assert(k_kbm_key[BIND_L] == SDL_SCANCODE_Q);
    assert(k_kbm_key[BIND_A] == SDL_SCANCODE_E && k_kbm_key[BIND_B] == SDL_SCANCODE_LSHIFT);
    assert(k_kbm_key[BIND_X] == SDL_SCANCODE_SPACE && k_kbm_key[BIND_Y] == SDL_SCANCODE_F);
    assert(k_kbm_key[BIND_ZR] == SDL_SCANCODE_R && k_kbm_key[BIND_PLUS] == SDL_SCANCODE_RETURN);
    assert(k_kbm_key[BIND_MINUS] == SDL_SCANCODE_M && k_kbm_key[BIND_DOWN] == SDL_SCANCODE_TAB);
    for (int i = BIND_RS_UP; i <= BIND_RS_RIGHT; i++) assert(!k_kbm_key[i]);  // the mouse drives the right stick
    puts("kbm preset ok");
    return 0;
}
