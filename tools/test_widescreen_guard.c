// Widescreen guard (runtime/hle/widescreen.c): "Unlocked" only widens the view when the loaded game version has the
// camera's aspect sites patched (GA ASPECT_SITES_0 / ASPECT_LITERAL non-zero). Without them the output is stretched
// from 16:9 (cutscene ghosting at 32:9), so widescreen_allowed() must stay false. tools/test_widescreen_guard.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "widescreen.c"  // (the unit itself, with the few host symbols it names stubbed below)

Settings g_settings;
const GameAddrs *g_ga;
uint64_t renderer_present_count(void) { return 0; }
uint64_t renderer_exec_frame(void) { return 0; }
int game_in_play(void) { return 0; }

static int failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
    } while (0)

int main(void) {
    static GameAddrs ga;  // (all zero: a version with no aspect sites, like both tables today)
    g_settings.aspect = ASPECT_UNLOCKED;

    g_ga = NULL;  // no version selected (start screen)
    CHECK(!widescreen_sites_present());
    CHECK(!widescreen_allowed());

    g_ga = &ga;
    CHECK(!widescreen_sites_present());
    CHECK(!widescreen_allowed());  // the stretch case: Unlocked, but nothing to widen

    ga.ASPECT_SITES_0 = 0x1000;  // one without the other is still not enough
    CHECK(!widescreen_sites_present());
    CHECK(!widescreen_allowed());
    ga.ASPECT_SITES_0 = 0, ga.ASPECT_LITERAL = 0x2000;
    CHECK(!widescreen_sites_present());

    ga.ASPECT_SITES_0 = 0x1000;
    CHECK(widescreen_sites_present());
    CHECK(widescreen_allowed());

    g_settings.aspect = ASPECT_ORIGINAL;
    CHECK(!widescreen_allowed());

    if (failures) return 1;
    puts("all checks passed");
    return 0;
}
