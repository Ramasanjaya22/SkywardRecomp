#pragma once
// Mouse as a sword (setting kbm input). Pure and single-threaded: the caller serializes motion and step with
// one mutex. Mouse motion becomes a virtual right stick: camera velocity, an 8-direction slash flick on LMB,
// a thrust pulse on MMB, ZL+R on RMB and an analog aim on M4. No SDL and no libm (see ms_len, ms_snap).
#include <stdbool.h>
#include <stdint.h>

enum { MS_CAM_WINDOW_MS = 50, MS_SLASH_WINDOW_MS = 100, MS_FLICK_MS = 70, MS_THRUST_MS = 60,
       MS_MAX_DELTA = 200, MS_RING = 128 };  // all tunable (Task 7)

typedef struct { bool lmb, mmb, rmb, aim; } MouseBtns;  // aim = M4 held
typedef struct { bool camera, combat; float sens; } MouseCfg;
typedef struct {
    int32_t rx, ry;     // virtual right stick, pad scale, up = +ry
    bool no_cam;        // flick or aim in flight: Free Camera must not add L
    bool rstick_click;  // thrust (MMB)
    bool zl, r;         // RMB
    bool use_item;      // LMB while aiming
} MouseOut;

typedef struct { uint64_t ms; float dx, dy; } MouseSample;
typedef struct MouseSword {
    MouseSample ring[MS_RING];  // recent motion, newest at head-1; samples in the same ms are merged
    uint32_t head, count;
    float pend_x, pend_y;       // motion since the last step, for the aim position
    float aim_x, aim_y;         // aim stick, up = +y
    int32_t last_x, last_y;     // last slash direction, each -1/0/1, up = +y
    bool has_last;
    int32_t flick_x, flick_y;   // direction of the flick in flight, -1/0/1
    uint64_t flick_until, thrust_until;
    bool lmb, mmb;              // previous poll, for the press edges
} MouseSword;

enum { MS_STICK = 32767, MS_DIAG = 23170 };  // 32767 / sqrt(2)

static inline void mouse_sword_reset(MouseSword *m) { *m = (MouseSword){0}; }

static inline float ms_abs(float v) { return v < 0 ? -v : v; }
static inline float ms_clampf(float v, float lim) { return v > lim ? lim : v < -lim ? -lim : v; }
static inline int32_t ms_i32(float v) {  // clamp to the stick range, round to nearest; NaN -> 0
    if (v != v) return 0;
    v = ms_clampf(v, (float)MS_STICK);
    return (int32_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

// Vector length without libm: Newton from |x|+|y| (an upper bound within sqrt(2) of the answer).
static inline float ms_len(float x, float y) {
    float d = x * x + y * y, s = ms_abs(x) + ms_abs(y);
    if (s <= 0) return 0;
    for (int i = 0; i < 6; i++) s = 0.5f * (s + d / s);
    return s;
}

// Snap a motion sum to one of 8 directions (-1/0/1 each, up = +y) by comparing |dx| and |dy| against
// tan(22.5 deg) = 0.4142; no atan2.
static inline void ms_snap(float sx, float sy, int32_t *ox, int32_t *oy) {
    float ax = ms_abs(sx), ay = ms_abs(sy);
    int32_t gx = sx > 0 ? 1 : -1, gy = sy < 0 ? 1 : -1;  // mouse up (sy < 0) = stick up
    *ox = ay <= 0.4142f * ax ? gx : ax <= 0.4142f * ay ? 0 : gx;
    *oy = ax <= 0.4142f * ay ? gy : ay <= 0.4142f * ax ? 0 : gy;
}

static inline void ms_sum(const MouseSword *m, uint64_t now, uint64_t window, float *sx, float *sy) {
    *sx = *sy = 0;
    for (uint32_t i = 0; i < m->count; i++) {
        const MouseSample *s = &m->ring[(m->head + MS_RING - 1 - i) % MS_RING];
        if (s->ms > now) continue;
        if (now - s->ms >= window) break;  // older ones are older still
        *sx += s->dx;
        *sy += s->dy;
    }
}

// One SDL motion event; each axis is clamped to +-MS_MAX_DELTA so a warp or a spike cannot fling the stick.
static inline void mouse_sword_motion(MouseSword *m, float dx, float dy, uint64_t now_ms) {
    dx = ms_clampf(dx, (float)MS_MAX_DELTA);
    dy = ms_clampf(dy, (float)MS_MAX_DELTA);
    m->pend_x += dx;
    m->pend_y += dy;
    if (m->count) {
        MouseSample *last = &m->ring[(m->head + MS_RING - 1) % MS_RING];
        if (last->ms == now_ms) {
            last->dx += dx;
            last->dy += dy;
            return;
        }
    }
    // Samples in one ms merge, so a window holds at most one per ms: MS_RING must stay >= MS_SLASH_WINDOW_MS.
    // Only a non-monotonic now_ms (a clock that steps back) could still push the ring past that and drop the oldest.
    m->ring[m->head] = (MouseSample){now_ms, dx, dy};
    m->head = (m->head + 1) % MS_RING;
    if (m->count < MS_RING) m->count++;
}

static inline MouseOut mouse_sword_step(MouseSword *m, MouseBtns b, MouseCfg c, uint64_t now_ms) {
    MouseOut o = {0};
    // A bad sens (NaN, negative, inf from a hand-edited ini) must not reach the int cast: NaN/negative -> 0, cap at 8.
    c.sens = c.sens > 0 ? (c.sens > 8.0f ? 8.0f : c.sens) : 0;
    if (!c.combat) b = (MouseBtns){0};  // also lets the edge state fall back so a re-enable starts clean
    bool lmb_edge = b.lmb && !m->lmb, mmb_edge = b.mmb && !m->mmb;
    m->lmb = b.lmb;
    m->mmb = b.mmb;

    if (mmb_edge) m->thrust_until = now_ms + MS_THRUST_MS;
    o.rstick_click = now_ms < m->thrust_until;
    o.zl = o.r = b.rmb;

    if (b.aim) {
        // Aim: motion since the last step moves a stick position clamped to the circle. 300 px = full deflection.
        const float k = c.sens * (float)MS_STICK / 300.0f;
        m->aim_x += m->pend_x * k;
        m->aim_y -= m->pend_y * k;
        float len = ms_len(m->aim_x, m->aim_y);
        if (len > (float)MS_STICK) {
            m->aim_x *= (float)MS_STICK / len;
            m->aim_y *= (float)MS_STICK / len;
        }
        m->pend_x = m->pend_y = 0;
        m->count = 0;  // the aim motion must not drive the camera once aim is released
        o.rx = ms_i32(m->aim_x);
        o.ry = ms_i32(m->aim_y);
        o.no_cam = true;
        o.use_item = b.lmb;
        return o;
    }
    m->aim_x = m->aim_y = m->pend_x = m->pend_y = 0;

    if (lmb_edge) {
        float sx, sy;
        ms_sum(m, now_ms, MS_SLASH_WINDOW_MS, &sx, &sy);
        if (sx * sx + sy * sy >= 16.0f) {  // under 4 px counts as no motion: reuse the last direction
            ms_snap(sx, sy, &m->last_x, &m->last_y);
            m->has_last = true;
        } else if (!m->has_last) {
            m->last_x = 0;
            m->last_y = 1;  // default: vertical
            m->has_last = true;
        }
        m->flick_x = m->last_x;
        m->flick_y = m->last_y;
        m->flick_until = now_ms + MS_FLICK_MS;
    }
    if (now_ms < m->flick_until) {
        int32_t mag = (m->flick_x && m->flick_y) ? MS_DIAG : MS_STICK;
        o.rx = m->flick_x * mag;
        o.ry = m->flick_y * mag;
        o.no_cam = true;
    } else if (c.camera) {
        // px over MS_CAM_WINDOW_MS is a velocity; 2 px/ms is full deflection, so full = sum * 32767 / (window * 2).
        float sx, sy;
        ms_sum(m, now_ms, MS_CAM_WINDOW_MS, &sx, &sy);
        const float k = c.sens * (float)MS_STICK / (MS_CAM_WINDOW_MS * 2.0f);
        o.rx = ms_i32(sx * k);
        o.ry = ms_i32(-sy * k);
    }
    return o;
}
