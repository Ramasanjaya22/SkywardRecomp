// Frame rate setting (runtime/hle/framepace.c): what SS HD's frame controller is handed (tools/patches.py
// FRAME_CTRL_INTERVAL_SITE): its 10-frame average (>= 1.5: interval 2) and its timestep (1/30 s units).
// tools/test_fps_target.py.
#include <stdio.h>

#include "framepace.c"  // (the unit itself, with the few host symbols it names stubbed below)

Settings g_settings;
const GameAddrs *g_ga;
volatile uint64_t g_vsync_count;
static uint64_t t_ns = 1000000000ull;
uint64_t plat_now_ns(void) { return t_ns; }

static int failures;
#define CHECK(c)                                                                                 \
    do {                                                                                         \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
    } while (0)
static int NEAR(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }  // (a function: a is a call, once)

int main(void) {
    static GameAddrs ga;
    ga.FRAME_CTRL_INTERVAL_SITE = 0x1000, ga.FRAME_TIMESTEP = 0x2000;
    g_ga = &ga;

    g_settings.fps_target = FPS_AUTO;  // the game's own choice, untouched
    CHECK(NEAR(rt_frame_interval_avg(1.7f), 1.7f));
    CHECK(NEAR(rt_frame_timestep(0.5f), 0.5f));
    CHECK(NEAR(rt_frame_timestep(1.0f), 1.0f));

    g_settings.fps_target = FPS_30;  // always interval 2, a full 1/30 s step
    CHECK(rt_frame_interval_avg(1.0f) >= 1.5f);
    CHECK(NEAR(rt_frame_timestep(0.5f), 1.0f));

    g_settings.fps_target = FPS_60;  // never interval 2, even when the game's frames were slow
    CHECK(rt_frame_interval_avg(2.0f) < 1.5f);
    CHECK(NEAR(rt_frame_timestep(1.0f), 0.5f));

    g_settings.fps_target = FPS_90;
    CHECK(rt_frame_interval_avg(2.0f) < 1.5f);
    CHECK(NEAR(rt_frame_timestep(0.5f), 1.0f / 3));

    g_settings.fps_target = FPS_120;
    CHECK(rt_frame_interval_avg(2.0f) < 1.5f);
    CHECK(NEAR(rt_frame_timestep(0.5f), 0.25f));

    g_settings.fps_target = FPS_UNLOCKED;  // the time that passed, 1/8 to 1 step
    CHECK(rt_frame_interval_avg(2.0f) < 1.5f);
    rt_frame_timestep(0.5f);
    t_ns += 500000000ull;  // a 500 ms stall: at most one 1/30 s step
    CHECK(NEAR(rt_frame_timestep(0.5f), 1.0f));
    t_ns += 10000000ull;  // 10 ms: 0.3 steps
    CHECK(NEAR(rt_frame_timestep(0.5f), 0.3f));
    t_ns += 1000000ull;  // 1 ms: at least 1/8
    CHECK(NEAR(rt_frame_timestep(0.5f), 0.125f));

    g_settings.fps_target = FPS_120;  // a version without the controller site: Auto
    ga.FRAME_CTRL_INTERVAL_SITE = 0;
    CHECK(rt_fps_target() == FPS_AUTO);
    CHECK(NEAR(rt_frame_interval_avg(2.0f), 2.0f));
    CHECK(NEAR(rt_frame_timestep(1.0f), 1.0f));
    g_ga = NULL;  // no version selected yet
    CHECK(rt_fps_target() == FPS_AUTO);

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
