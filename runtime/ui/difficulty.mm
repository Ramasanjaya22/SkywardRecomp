// Gameplay tab (was Difficulty): walk speed (runtime/hle/qol.c rt_walk_speed), damage taken multiplier / one-hit KO
// (applied where the game computes Link's damage, see tools/patches.py DAMAGE_*_SITES); rt_hero_mode() reads the game's own Hero Mode flag (PlayerStatus+0x120 = save
// 0x55E0). Settings persist in larecomp.ini (g_settings.diff_*).
#include "../game_addresses.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "option_rows.h"

extern "C" {
#include "../hle.h"
#include "../settings.h"
int game_in_play(void);
int rt_hookshot_seed_forced(void);  // hle/hookshot.c
int rt_no_buff_seed_forced(void);   // hle/qol.c
#include "../livesplit.h"
}


#define HERO_FLAG (GA(LIVE_SAVE) + 0x55E0)  // PlayerStatus (save+0x54C0) + 0x120

extern "C" int rt_hero_mode(void) { return *(volatile uint8_t *)(uintptr_t)HERO_FLAG == 1; }

// damage in quarter hearts before the Hero Mode shift; returns the damage to subtract
extern "C" int32_t rt_damage(int32_t dmg, int32_t shift) {
    static int envd = -1;  // LARECOMP_DAMAGE=<mult> / ohko: test override
    if (envd < 0) {
        const char *e = getenv("LARECOMP_DAMAGE");
        envd = e != NULL;
        if (e) {
            if (!strcmp(e, "ohko")) g_settings.diff_ohko = true;
            else g_settings.diff_damage = (float)atof(e);
        }
    }
    int32_t base = dmg << (shift & 31);
    if (base <= 0) return base;  // healing / zero: untouched
    if (g_settings.diff_ohko) return 999;
    float m = g_settings.diff_damage > 0 ? g_settings.diff_damage : 1.0f;
    int32_t r = (int32_t)lroundf((float)base * m);
    if (ENV("LARECOMP_LOG_DAMAGE")) LOG("[difficulty] damage %d (shift %d) -> %d\n", dmg, shift, r < 1 ? 1 : r);
    return r < 1 ? 1 : r;  // at least a quarter heart
}

// Once per presented frame (from save_editor_frame). (The "Hero Mode on every file" lever, g_settings.diff_hero, left
// the Gameplay tab - user, 2026-10-11 - and is no longer applied, so nobody is stuck with it; a file started in Hero
// Mode keeps the game's own flag.)
extern "C" void difficulty_frame(void) {}

// Plain levers, each at the game's own value by default (Default, off), as option rows (option_rows.h).
extern "C" void difficulty_draw(float scale) {
    opt_begin("##gameplay", scale);
    opt_section("General");
    {
        // what the game is told the console's language is: its text follows this (runtime/settings.c)
        char autol[64];
        snprintf(autol, sizeof autol, "Auto (%s)", settings_language_name());
        const char *names[N_LANGUAGES];
        names[0] = autol;
        for (int i = 1; i < N_LANGUAGES; i++) names[i] = g_language_names[i];
        if (opt_choice("Language", &g_settings.language, names, N_LANGUAGES, LANG_AUTO,
                       "The game's text language, as its own system setting. Applies when the game restarts."))
            settings_save();
    }
    if (opt_toggle("Mirror mode", &g_settings.mirror_mode, false, "Mirrors the game world."))
        settings_save();
    // (LA's levers - hookshot, Guardian Acorns, walk speed, damage, timer, LiveSplit - are gone: their
    // patch sites are LA addresses, 0 in SS)
    opt_end();
}
