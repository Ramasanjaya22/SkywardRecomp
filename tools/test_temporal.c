// The temporal upscaler inputs (runtime/nvn/temporal.c): Halton jitter, phase count, LOD bias, and the camera jitter on a
// copy of the scene UBO. No game files, no window. Build + run: tools/test_temporal.py.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../runtime/nvn/temporal.h"

static bool close_to(float a, float b) { return fabsf(a - b) < 1e-5f; }

// NDC x, y of point p through the row-major matrix m (clip = m * p)
static void ndc(const float *m, const float *p, float *x, float *y) {
    float c[4];
    for (int r = 0; r < 4; r++) c[r] = m[4 * r] * p[0] + m[4 * r + 1] * p[1] + m[4 * r + 2] * p[2] + m[4 * r + 3] * p[3];
    *x = c[0] / c[3];
    *y = c[1] / c[3];
}

static void mat_mul(const double a[16], const double b[16], double out[16]) {
    double t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[4 * i + k] * b[4 * k + j];
            t[4 * i + j] = s;
        }
    memcpy(out, t, sizeof t);
}

// GL-style perspective, row-major (clip = m * p): y up, NDC z in [-1, 1], row 3 = (0, 0, -1, 0)
static void perspective(double fovy, double aspect, double n, double f, double m[16]) {
    double t = 1 / tan(fovy / 2);
    const double p[16] = {t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, (f + n) / (n - f), 2 * f * n / (n - f), 0, 0, -1, 0};
    memcpy(m, p, sizeof p);
}

// camera at (x, y, z) looking down -z and turned by yaw about y; row-major view matrix
static void view_at(double x, double y, double z, double yaw, double v[16]) {
    double c = cos(yaw), s = sin(yaw);
    const double r[16] = {c, 0, s, -(c * x + s * z), 0, 1, 0, -y, -s, 0, c, -(-s * x + c * z), 0, 0, 0, 1};
    memcpy(v, r, sizeof r);
}

// row (y down) and NDC z of world point p through vp; the rows grow with the game's NDC y (lower-left origin)
static void to_pixel(const double vp[16], const double p[4], double w, double h, double out[3]) {
    double c[4];
    for (int r = 0; r < 4; r++) c[r] = vp[4 * r] * p[0] + vp[4 * r + 1] * p[1] + vp[4 * r + 2] * p[2] + vp[4 * r + 3] * p[3];
    out[0] = (c[0] / c[3] + 1) * w / 2;
    out[1] = (1 + c[1] / c[3]) * h / 2;
    out[2] = c[2] / c[3];
}

// view-only camera turned by yaw about y (float, as the scene UBO)
static void yaw_view(float yaw, float v[16]) {
    float c = cosf(yaw), s = sinf(yaw);
    const float r[16] = {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1};
    memcpy(v, r, sizeof r);
}

int main(void) {
    assert(close_to(temporal_halton(1, 2), 0.5f) && close_to(temporal_halton(2, 2), 0.25f) && close_to(temporal_halton(3, 2), 0.75f));
    assert(close_to(temporal_halton(1, 3), 1.0f / 3) && close_to(temporal_halton(2, 3), 2.0f / 3));

    // ceil(8 * (display / render)^2): 2.0x 32, 3.0x 72, 1.5x 18; 1.7x is 23.12, rounded up
    assert(temporal_phase_count(960, 1920) == 32);
    assert(temporal_phase_count(640, 1920) == 72);
    assert(temporal_phase_count(1280, 1920) == 18);
    assert(temporal_phase_count(1000, 1700) == 24);
    assert(temporal_phase_count(1920, 1920) == 8);

    // offsets within [-0.5, 0.5), repeating every `phases` frames; frame 0 is Halton index 1: (0, 1/3 - 0.5)
    for (uint64_t f = 0; f < 200; f++) {
        float jx, jy, px, py;
        temporal_jitter(f, 32, &jx, &jy);
        temporal_jitter(f + 32, 32, &px, &py);
        assert(jx >= -0.5f && jx < 0.5f && jy >= -0.5f && jy < 0.5f);
        assert(px == jx && py == jy);
    }
    float jx0, jy0;
    temporal_jitter(0, 32, &jx0, &jy0);
    assert(close_to(jx0, 0.0f) && close_to(jy0, 1.0f / 3 - 0.5f));

    assert(close_to(temporal_lod_bias(960, 1920), -1.0f) && close_to(temporal_lod_bias(1920, 1920), 0.0f) &&
           close_to(temporal_lod_bias(1920, 960), 1.0f));

    temporal_note_output(3840);
    assert(temporal_output_width() == 3840);

    // scene camera: view = identity, projection with row 3 = (0, 0, -1, 0), view-projection = projection
    float u[80] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float proj[16] = {1.5f, 0, 0, 0, 0, 2.0f, 0, 0, 0, 0, -1.2f, -0.2f, 0, 0, -1, 0};
    memcpy(u + 16, proj, sizeof proj);
    memcpy(u + 32, proj, sizeof proj);
    const float p[4] = {1, 2, -10, 1};
    float x0, y0, x1, y1, x2, y2;
    ndc(u + 32, p, &x0, &y0);  // (0.15, 0.4)
    float jx, jy;
    temporal_jitter(1, 32, &jx, &jy);  // frame 1: Halton index 2, (-0.25, 1/6)
    // the image rows move +jx and +jy: NDC y goes up by 2 jy / h for lower-left (rows grow with it), down for upper-left
    float ll[80], ul[80];
    memcpy(ll, u, sizeof ll);
    memcpy(ul, u, sizeof ul);
    assert(temporal_jitter_camera(ll, 1, 32, 1920, 1080, true) && temporal_jitter_camera(ul, 1, 32, 1920, 1080, false));
    assert(memcmp(ll, u, 16 * sizeof(float)) == 0);  // the view is not touched
    ndc(ll + 32, p, &x1, &y1);
    assert(close_to(x1 - x0, 2 * jx / 1920) && close_to(y1 - y0, 2 * jy / 1080));
    ndc(ll + 16, p, &x1, &y1);  // the projection gets the same shift
    assert(close_to(x1 - x0, 2 * jx / 1920) && close_to(y1 - y0, 2 * jy / 1080));
    ndc(ul + 32, p, &x2, &y2);
    assert(close_to(x2 - x0, 2 * jx / 1920) && close_to(y2 - y0, -2 * jy / 1080));

    // not a perspective camera: unchanged
    float w[80], before[80];
    memcpy(w, u, sizeof w);
    w[30] = 0;  // projection row 3 is no longer (0, 0, -1, 0)
    memcpy(before, w, sizeof w);
    bool changed = temporal_jitter_camera(w, 1, 32, 1920, 1080, true);
    assert(!changed);
    assert(memcmp(w, before, sizeof w) == 0);

    // reprojection: a static camera gives M = identity; a singular view-projection gives no M
    double dproj[16], view_prev[16], vp_prev[16], m[16];
    perspective(1.0, 16.0 / 9, 0.1, 100, dproj);
    view_at(0, 0, 0, 0, view_prev);
    mat_mul(dproj, view_prev, vp_prev);
    assert(temporal_reprojection(vp_prev, vp_prev, m));
    for (int i = 0; i < 16; i++) assert(fabs(m[i] - (i % 5 == 0 ? 1.0 : 0.0)) < 1e-9);
    double zero[16] = {0}, flat[16] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // rows 0 and 1 equal
    assert(!temporal_reprojection(zero, vp_prev, m) && !temporal_reprojection(flat, vp_prev, m));

    // the camera moves right by 0.5: M from the current and previous view-projection must match projecting each point
    // directly; the motion is previous minus current
    double view_right[16], vp_right[16];
    view_at(0.5, 0, 0, 0, view_right);
    mat_mul(dproj, view_right, vp_right);
    assert(temporal_reprojection(vp_right, vp_prev, m));
    const double pts[3][4] = {{0, 0, -10, 1}, {2, 1, -20, 1}, {-3, -1.5, -8, 1}};
    for (int i = 0; i < 3; i++) {
        double cur[3], prev[3], got[2], alt[2];
        to_pixel(vp_right, pts[i], 1920, 1080, cur);
        to_pixel(vp_prev, pts[i], 1920, 1080, prev);
        double z = temporal_depth_ndc((cur[2] + 1) / 2, 0, 1, true);  // NVN's [-1, 1] depth, stored as [0, 1]
        assert(fabs(z - cur[2]) < 1e-9);
        temporal_motion_px(m, 1920, 1080, true, cur[0], cur[1], z, got);
        assert(fabs(got[0] - (prev[0] - cur[0])) < 1e-6 && fabs(got[1] - (prev[1] - cur[1])) < 1e-6);
        // the upper-left row is h minus the lower-left row, so the same point's y motion is negated
        temporal_motion_px(m, 1920, 1080, false, cur[0], 1080 - cur[1], z, alt);
        assert(fabs(alt[0] - got[0]) < 1e-6 && fabs(alt[1] + got[1]) < 1e-6);
    }
    // sign: the camera moved right, so the centre point is at the left now and its previous pixel is to the right
    double cur[3], got[2];
    to_pixel(vp_right, pts[0], 1920, 1080, cur);
    temporal_motion_px(m, 1920, 1080, true, cur[0], cur[1], cur[2], got);
    assert(got[0] > 0 && fabs(got[1]) < 1e-6);
    // camera moved up: the centre point is now lower, and the rows grow with the game's y, so the motion in y is positive
    double view_up[16], vp_up[16], m_up[16];
    view_at(0, 0.5, 0, 0, view_up);
    mat_mul(dproj, view_up, vp_up);
    assert(temporal_reprojection(vp_up, vp_prev, m_up));
    to_pixel(vp_up, pts[0], 1920, 1080, cur);
    temporal_motion_px(m_up, 1920, 1080, true, cur[0], cur[1], cur[2], got);
    assert(got[1] > 0 && fabs(got[0]) < 1e-6);
    // stored depth: t = (d - zn) / (zf - zn) is the Vulkan depth, NVN's [-1, 1] is 2t - 1
    assert(close_to(temporal_depth_ndc(0.5, 0.25, 0.75, true), 0) && close_to(temporal_depth_ndc(0.5, 0.25, 0.75, false), 0.5));
    assert(close_to(temporal_depth_ndc(0.75, 0.25, 0.75, true), 1) && close_to(temporal_depth_ndc(0.25, 0, 1, false), 0.25));

    // camera cut: the turn is over 60 degrees; a move without a turn is not a cut (documented in temporal.h)
    float v0[16], v59[16], v61[16], v90[16], vmove[16];
    yaw_view(0, v0);
    yaw_view((float)(59.0 * 3.14159265358979323846 / 180), v59);
    yaw_view((float)(61.0 * 3.14159265358979323846 / 180), v61);
    yaw_view((float)(90.0 * 3.14159265358979323846 / 180), v90);
    memcpy(vmove, v0, sizeof vmove);
    vmove[3] = -10;
    assert(!temporal_camera_cut(v0, v0) && !temporal_camera_cut(v0, v59) && !temporal_camera_cut(v0, vmove));
    assert(temporal_camera_cut(v0, v61) && temporal_camera_cut(v0, v90));

    printf("test_temporal: ok\n");
    return 0;
}
