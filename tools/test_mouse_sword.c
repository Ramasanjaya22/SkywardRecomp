#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include "hle/mouse_sword.h"

static MouseOut step(MouseSword *m, MouseBtns b, uint64_t t) {
    MouseCfg c = {true, true, 1.0f};
    return mouse_sword_step(m, b, c, t);
}

static void near(int32_t got, int32_t want) { assert(got >= want - 1 && got <= want + 1); }

int main(void) {
    const MouseBtns none = {0};
    const MouseBtns lmb = {.lmb = true};
    const MouseBtns mmb = {.mmb = true};
    const MouseBtns rmb = {.rmb = true};
    const MouseBtns aim = {.aim = true};
    const MouseBtns aim_lmb = {.aim = true, .lmb = true};
    MouseOut o;

    // Camera: the last MS_CAM_WINDOW_MS of motion is a velocity, 2 px/ms is full deflection.
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 100, 0, 0);
        o = step(&m, none, 10);
        assert(o.rx == 32767 && o.ry == 0 && !o.no_cam);
        o = step(&m, none, 200);
        assert(o.rx == 0);
    }
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 0, -100, 0);  // mouse up = stick up
        o = step(&m, none, 10);
        assert(o.ry == 32767 && o.rx == 0);
    }
    {
        // Brief said motion(50,0) -> 16383 at sens 0.5, but the formula gives 8191 for 50 and 16383 for 100.
        MouseSword m = {0};
        MouseCfg c = {true, true, 0.5f};
        mouse_sword_motion(&m, 50, 0, 0);
        o = mouse_sword_step(&m, none, c, 10);
        near(o.rx, 8191);
        MouseSword n = {0};
        mouse_sword_motion(&n, 100, 0, 0);
        o = mouse_sword_step(&n, none, c, 10);
        near(o.rx, 16383);
    }
    {
        MouseSword m = {0};
        MouseCfg c = {false, true, 1.0f};
        mouse_sword_motion(&m, 100, 100, 0);
        o = mouse_sword_step(&m, none, c, 10);
        assert(o.rx == 0 && o.ry == 0);
    }

    // Slash: LMB press snaps the last MS_SLASH_WINDOW_MS of motion to 8 directions and holds it MS_FLICK_MS.
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 80, 0, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == 32767 && o.ry == 0 && o.no_cam);
        o = step(&m, lmb, 50 + MS_FLICK_MS - 1);
        assert(o.no_cam);
        o = step(&m, lmb, 50 + MS_FLICK_MS + 1);  // still held: no second flick
        assert(!o.no_cam && o.rx == 0);
        o = step(&m, lmb, 400);
        assert(!o.no_cam && o.rx == 0 && o.ry == 0);
    }
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, -80, 0, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == -32767 && o.ry == 0);
    }
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 0, 80, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == 0 && o.ry == -32767);
    }
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 70, -70, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == 23170 && o.ry == 23170);
    }

    // Click with no motion: vertical by default, then the last direction.
    {
        MouseSword m = {0};
        o = step(&m, lmb, 50);
        assert(o.ry == 32767 && o.rx == 0 && o.no_cam);
    }
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 80, 0, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == 32767);
        o = step(&m, none, 200);
        o = step(&m, lmb, 500);  // no motion in the window: reuse right
        assert(o.rx == 32767 && o.ry == 0 && o.no_cam);
    }

    // combat off: the buttons do nothing.
    {
        MouseSword m = {0};
        MouseCfg c = {true, false, 1.0f};
        MouseBtns all = {true, true, true, true};
        mouse_sword_motion(&m, 80, 0, 0);
        o = mouse_sword_step(&m, all, c, 10);
        assert(!o.rstick_click && !o.zl && !o.r && !o.use_item && !o.no_cam);
        near(o.rx, 26214);  // only the camera: 80 px / 50 ms
    }

    // Thrust: one pulse per MMB press.
    {
        MouseSword m = {0};
        o = step(&m, mmb, 0);
        assert(o.rstick_click);
        o = step(&m, mmb, MS_THRUST_MS - 1);
        assert(o.rstick_click);
        o = step(&m, mmb, MS_THRUST_MS + 1);
        assert(!o.rstick_click);
        o = step(&m, none, MS_THRUST_MS + 5);
        assert(!o.rstick_click);
        o = step(&m, mmb, 500);
        assert(o.rstick_click);
    }

    // RMB: target + shield while held.
    {
        MouseSword m = {0};
        o = step(&m, rmb, 0);
        assert(o.zl && o.r);
        o = step(&m, none, 10);
        assert(!o.zl && !o.r);
    }

    // Aim (M4): the mouse is an analog stick, camera is off, LMB uses the item.
    {
        MouseSword m = {0};
        o = step(&m, aim, 0);
        mouse_sword_motion(&m, 150, 0, 0);
        o = step(&m, aim, 5);
        near(o.rx, 16383);
        assert(o.ry == 0 && o.no_cam);
        mouse_sword_motion(&m, 200, 0, 6);
        mouse_sword_motion(&m, 200, 0, 6);
        mouse_sword_motion(&m, 0, -200, 6);
        mouse_sword_motion(&m, 0, -200, 6);
        mouse_sword_motion(&m, 0, -200, 6);
        o = step(&m, aim, 7);
        assert(o.rx > 0 && o.ry > 0);
        assert((int64_t)o.rx * o.rx + (int64_t)o.ry * o.ry <= (int64_t)32768 * 32768);
        o = step(&m, none, 10);  // released: back to the camera, nothing left over
        assert(o.rx == 0 && o.ry == 0 && !o.no_cam);
    }
    {
        MouseSword m = {0};
        o = step(&m, aim, 0);
        mouse_sword_motion(&m, 10000, 0, 0);  // counts as MS_MAX_DELTA
        o = step(&m, aim, 5);
        near(o.rx, 21844);
    }
    {
        MouseSword m = {0};
        o = step(&m, aim, 0);
        o = step(&m, aim_lmb, 10);
        assert(o.use_item && o.rx == 0 && o.ry == 0 && o.no_cam);
        o = step(&m, aim, 20);
        assert(!o.use_item);
    }

    // Reset: the remembered direction and any swing in flight are gone.
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 80, 0, 0);
        o = step(&m, lmb, 50);
        assert(o.rx == 32767);
        mouse_sword_reset(&m);
        o = step(&m, none, 60);
        assert(!o.no_cam && o.rx == 0 && o.ry == 0);
        o = step(&m, lmb, 1000);
        assert(o.ry == 32767 && o.rx == 0);
    }

    // A 1 kHz mouse (1 px/ms for 200 ms): the ring must hold every window, camera and slash alike.
    {
        MouseSword m = {0};
        for (uint64_t t = 0; t < 200; t++) mouse_sword_motion(&m, 1, 0, t);
        o = step(&m, none, 199);
        near(o.rx, 16383);  // 50 px in the 50 ms window
        o = step(&m, lmb, 199);  // 100 px in the 100 ms window: right
        assert(o.rx == 32767 && o.ry == 0 && o.no_cam);
    }

    // A NaN or infinite sens (a bad ini) must not reach the int cast or poison the aim position.
    {
        const float bad[2] = {NAN, INFINITY};
        for (int i = 0; i < 2; i++) {
            MouseCfg c = {true, true, bad[i]};
            MouseSword m = {0};
            o = mouse_sword_step(&m, none, c, 0);  // no motion
            assert(o.rx == 0 && o.ry == 0);
            mouse_sword_motion(&m, 100, -100, 1);
            o = mouse_sword_step(&m, none, c, 2);  // camera
            assert(o.rx >= -32767 && o.rx <= 32767 && o.ry >= -32767 && o.ry <= 32767);
            o = mouse_sword_step(&m, aim, c, 3);  // aim, no motion
            assert(o.rx == 0 && o.ry == 0);
            mouse_sword_motion(&m, 150, 150, 4);
            o = mouse_sword_step(&m, aim, c, 5);  // aim with motion
            assert(o.rx >= -32767 && o.rx <= 32767 && o.ry >= -32767 && o.ry <= 32767);
            o = mouse_sword_step(&m, none, c, 6);  // released: recovers
            assert(o.rx == 0 && o.ry == 0 && !o.no_cam);
            MouseCfg ok = {true, true, 1.0f};
            o = mouse_sword_step(&m, aim, ok, 7);
            mouse_sword_motion(&m, 150, 0, 7);
            o = mouse_sword_step(&m, aim, ok, 8);
            near(o.rx, 16383);  // aim works again with a sane sens
        }
    }

    // After a flick or thrust the camera leaves out the motion made until it ended: no lurch, later motion counts.
    {
        MouseSword m = {0};
        mouse_sword_motion(&m, 80, 0, 40);
        o = step(&m, lmb, 50);  // flick until 50 + MS_FLICK_MS
        mouse_sword_motion(&m, 60, 0, 50 + MS_FLICK_MS - 10);  // follow-through inside the flick
        o = step(&m, none, 50 + MS_FLICK_MS + 1);
        assert(!o.no_cam && o.rx == 0 && o.ry == 0);
        mouse_sword_motion(&m, 100, 0, 50 + MS_FLICK_MS + 5);
        o = step(&m, none, 50 + MS_FLICK_MS + 10);
        assert(o.rx == 32767);
    }
    {
        MouseSword m = {0};
        o = step(&m, mmb, 0);  // thrust until MS_THRUST_MS
        mouse_sword_motion(&m, 100, 0, MS_THRUST_MS - 10);
        o = step(&m, none, MS_THRUST_MS + 1);
        assert(o.rx == 0);
    }
    return 0;
}
