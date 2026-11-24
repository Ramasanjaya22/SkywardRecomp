// Frame pacing: the game's speed doesn't depend on the frame rate.
// The game advances its world each frame by its frame period (16.67 ms) times the swap interval its frame timer
// (f_700000c910) reads from the system object - normally 1. When a frame takes longer than a vsync (GPU-bound at
// 32:9, a hitch), the next frame starts a vsync later but still advanced only 16.67 ms: slow motion (30 fps = half
// speed). tools/patches.py FRAME_STEPS_SITE routes that read here: we return how many vsyncs really passed since
// the last frame, so the world advances by the time that passed. The game's own waiting is unaffected (it reads
// the interval nowhere else). Shared by the macOS and Windows builds.
// FINDING (2026-10-09): forcing 2 steps did NOT change Link's walking speed - gameplay is a true fixed step per
// frame; this timer feeds something else. Kept for research, off by default. (2026-10-18: the read is the game's
// dynamic resolution's frame budget - tools/patches.py GAME_DYNRES_SITE, now off, so it no longer runs at all.)
//   LARECOMP_FRAMEPACE=1     hand the timer the vsyncs elapsed (default: the game's own value)
//   LARECOMP_FRAME_STEPS_MAX maximum steps per frame (default 4: after a long stall the game doesn't jump ahead)
//   LARECOMP_FORCE_STEPS=<n> (test) always n
#include <stdlib.h>

#include "../game_addresses.h"
#include "../hle.h"
#include "../settings.h"

extern volatile uint64_t g_vsync_count;  // runtime/hle/vi.c
uint64_t g_frame_steps_late;             // (stats) frames that advanced more than one step

uint32_t rt_frame_steps(uint32_t interval) {
    static int on = -1, max_steps, forced;
    if (on < 0) {
        const char *e = ENV("LARECOMP_FRAMEPACE");
        on = e ? atoi(e) != 0 : 0;  // off by default: the world (Link's movement) doesn't use this step (tested)
        e = ENV("LARECOMP_FRAME_STEPS_MAX");
        max_steps = e ? atoi(e) : 4;
        if (max_steps < 1) max_steps = 1;
        e = ENV("LARECOMP_FORCE_STEPS");
        forced = e ? atoi(e) : 0;
    }
    static int ncall;
    if (ENV("LARECOMP_LOG_FRAMEPACE") && ncall++ % 120 == 0) LOG("[framepace] call %d interval %u vsync %llu\n", ncall, interval, (unsigned long long)g_vsync_count);
    if (forced > 0) return (uint32_t)forced;
    static uint64_t last;
    uint64_t now = __atomic_load_n(&g_vsync_count, __ATOMIC_RELAXED);
    uint64_t steps = last ? now - last : 1;
    last = now;
    if (!on) return interval;
    if (steps < interval) steps = interval;  // (the game's own 30 fps mode: it waits that many vsyncs anyway)
    if (steps < 1) steps = 1;
    if (steps > (uint64_t)max_steps) steps = (uint64_t)max_steps;
    if (steps > 1) g_frame_steps_late++;
    return (uint32_t)steps;
}

// The frame rate setting (Settings.fps_target). SS HD's frame controller (tools/patches.py FRAME_CTRL_INTERVAL_SITE)
// averages its last 10 frames' time in vsyncs: 1.5 or more picks present interval 2 and a world timestep of 1.0 (in
// 1/30 s), else interval 1 and 0.5 (its FRAME_TIMESTEP global). With a target, the average reads as 2 (30) or 1 (the
// rest: the game never drops itself to 30), and the timestep is the target's frame time - or, Unlocked, the time that
// really passed since the last frame (1/8 to 1 step: a stall doesn't jump the world ahead). runtime/nvn/pace.c paces
// the frames to the target. Auto, or a game version without the site: the game's own values.
int rt_fps_target(void) { return g_ga && GA(FRAME_CTRL_INTERVAL_SITE) ? g_settings.fps_target : FPS_AUTO; }

float rt_frame_interval_avg(float game_avg) {
    int t = rt_fps_target();
    return t == FPS_AUTO ? game_avg : t == FPS_30 ? 2.0f : 1.0f;
}

float rt_frame_timestep(float game_value) {
    static uint64_t last;
    uint64_t now = plat_now_ns(), dt = last ? now - last : 16666667ull;
    last = now;
    int t = rt_fps_target();
    if (t == FPS_AUTO) return game_value;
    if (t > 0) return 30.0f / (float)t;
    float s = (float)dt * 3e-8f;  // (ns -> 1/30 s)
    return s < 0.125f ? 0.125f : s > 1.0f ? 1.0f : s;
}
