#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "temporal.h"

static uint32_t g_output_w;

float temporal_halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    for (; index; index /= base) {
        f /= (float)base;
        r += f * (float)(index % base);
    }
    return r;
}

uint32_t temporal_phase_count(uint32_t render_w, uint32_t display_w) {
    if (!render_w || !display_w) return 8;
    uint64_t r2 = (uint64_t)render_w * render_w, d2 = (uint64_t)display_w * display_w;
    return (uint32_t)((8 * d2 + r2 - 1) / r2);  // ceil(8 * (display / render)^2), in integers
}

void temporal_jitter(uint64_t frame, uint32_t phases, float *jx, float *jy) {
    uint32_t i = (uint32_t)(frame % (phases ? phases : 1)) + 1;
    *jx = temporal_halton(i, 2) - 0.5f;
    *jy = temporal_halton(i, 3) - 0.5f;
}

float temporal_lod_bias(uint32_t render_w, uint32_t display_w) {
    return render_w && display_w ? log2f((float)render_w / (float)display_w) : 0.0f;
}

void temporal_note_output(uint32_t display_w) { g_output_w = display_w; }
uint32_t temporal_output_width(void) { return g_output_w; }

bool temporal_jitter_camera(float *u, uint64_t frame, uint32_t phases, uint32_t render_w, uint32_t render_h, bool flip_y) {
    if (!render_w || !render_h) return false;
    if (u[12] != 0 || u[13] != 0 || u[14] != 0 || u[15] != 1 || u[30] != -1 || u[31] != 0) return false;  // perspective only
    float jx, jy;
    temporal_jitter(frame, phases, &jx, &jy);
    // the rows move +jy either way: set_viewport maps NDC y to rows downwards, and flip_y negates clip y
    float dx = 2 * jx / (float)render_w, dy = (flip_y ? 2 : -2) * jy / (float)render_h;
    for (int m = 16; m <= 32; m += 16)  // projection, view-projection: row 0 += dx * row 3, row 1 += dy * row 3
        for (int c = 0; c < 4; c++) {
            u[m + c] += dx * u[m + 12 + c];
            u[m + 4 + c] += dy * u[m + 12 + c];
        }
    return true;
}

// Gauss-Jordan with partial pivoting, row-major. False when a is singular (relative to its largest entry).
static bool mat4_inverse(const double a[16], double inv[16]) {
    double t[4][8], scale = 0;
    for (int i = 0; i < 16; i++)
        if (fabs(a[i]) > scale) scale = fabs(a[i]);
    if (!(scale > 0)) return false;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) t[i][j] = j < 4 ? a[4 * i + j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (fabs(t[r][c]) > fabs(t[p][c])) p = r;
        if (fabs(t[p][c]) <= 1e-12 * scale) return false;
        for (int j = 0; j < 8; j++) {
            double x = t[c][j];
            t[c][j] = t[p][j];
            t[p][j] = x;
        }
        double d = t[c][c];
        for (int j = 0; j < 8; j++) t[c][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            double f = t[r][c];
            for (int j = 0; j < 8; j++) t[r][j] -= f * t[c][j];
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) inv[4 * i + j] = t[i][4 + j];
    return true;
}

bool temporal_reprojection(const double vp_cur[16], const double vp_prev[16], double m[16]) {
    double inv[16];
    if (!mat4_inverse(vp_cur, inv)) return false;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += vp_prev[4 * i + k] * inv[4 * k + j];
            m[4 * i + j] = s;
        }
    return true;
}

double temporal_depth_ndc(double d, double zn, double zf, bool depth_minus_one) {
    double t = zf == zn ? 0 : (d - zn) / (zf - zn);
    return depth_minus_one ? 2 * t - 1 : t;
}

void temporal_motion_px(const double m[16], double vw, double vh, bool flip_y, double x, double y, double z, double out[2]) {
    double s = flip_y ? 1.0 : -1.0;  // the rows grow with the game's y when flipped, and shrink otherwise
    double v[4] = {2 * x / vw - 1, s * (2 * y / vh - 1), z, 1};
    double c[4];
    for (int i = 0; i < 4; i++) c[i] = m[4 * i] * v[0] + m[4 * i + 1] * v[1] + m[4 * i + 2] * v[2] + m[4 * i + 3] * v[3];
    double w = fabs(c[3]) < 1e-9 ? 1e-9 : c[3];
    out[0] = (c[0] / w + 1) * vw / 2 - x;
    out[1] = (s * (c[1] / w) + 1) * vh / 2 - y;
}

bool temporal_camera_cut(const float view_prev[16], const float view_cur[16]) {
    double a[3] = {-view_prev[8], -view_prev[9], -view_prev[10]}, b[3] = {-view_cur[8], -view_cur[9], -view_cur[10]};
    double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (la < 1e-12 || lb < 1e-12) return false;
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb) < 0.5;  // cos 60 degrees
}
