// Temporal upscaler inputs for AMD FSR 3/4 (and DLSS later): camera jitter per frame, its phase count, the mip LOD
// bias for a render scale, and the camera motion the reprojection uses. Pure math, no GPU or window: tools/test_temporal.py.
// The Vulkan backend applies the jitter only behind LARECOMP_VK_JITTER=1 (vulkan/exec.c cbuf_info), and computes the
// motion behind LARECOMP_VK_MOTION (vulkan/exec.c motion_*). With no upscaler that accumulates frames, the jitter shimmers
// and the motion has no consumer yet.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Halton sequence value of index (>= 1) in base, in (0, 1).
float temporal_halton(uint32_t index, uint32_t base);

// Jitter phases in one cycle: ceil(8 * (display / render)^2) from the widths (AMD FSR 2 docs: 2.0x is 32, 3.0x is 72).
uint32_t temporal_phase_count(uint32_t render_w, uint32_t display_w);

// Offset of frame `frame` in render pixels, each in [-0.5, 0.5): Halton 2 and 3 at index frame % phases + 1, centred.
// (As the FSR 2 sample's table; the sample source was not read, so check the table against it.)
void temporal_jitter(uint64_t frame, uint32_t phases, float *jx, float *jy);

// Mip LOD bias for a render scale: log2(render / display), negative when upscaling (AMD FSR docs). 0 without widths.
float temporal_lod_bias(uint32_t render_w, uint32_t display_w);

// The upscaled image's width in the window, noted once per presented frame (vulkan/device.c). 0 until then.
void temporal_note_output(uint32_t display_w);
uint32_t temporal_output_width(void);

// Jitters a copy of the scene camera UBO (320 bytes, row-major: view u[0..15], projection u[16..31], view-projection
// u[32..47]) when it is a perspective camera (the test widescreen_scene_ubo uses). clip = M * p: the offset goes into
// rows 0 and 1 of the projection and of the view-projection, as a multiple of row 3 (clip w). Result: NDC x += 2*jx /
// render_w, and NDC y += 2*jy / render_h when flip_y, -= 2*jy / render_h otherwise (vulkan/exec.c: the translator negates
// clip y when flip_y), so the image rows move +jx and +jy in both origins. The Y sign is derived from that, not checked
// in game. Returns false and leaves u unchanged when not perspective.
bool temporal_jitter_camera(float *u, uint64_t frame, uint32_t phases, uint32_t render_w, uint32_t render_h, bool flip_y);

// Camera motion for the reprojection (vulkan/exec.c motion_*). Matrices are row-major, clip = M * p with p = (x, y, z, 1).
// M = prev * inverse(cur) takes a point's current clip position to its previous one. False when cur is singular.
bool temporal_reprojection(const double vp_cur[16], const double vp_prev[16], double m[16]);

// NDC z of a stored depth d: the Vulkan depth in the viewport's [zn, zf] (set_viewport clamps both to [0, 1]), t =
// (d - zn) / (zf - zn). depth_minus_one: NVN's z is [-1, 1] and the translator stores (z + w) / 2, so z = 2t - 1.
double temporal_depth_ndc(double d, double zn, double zf, bool depth_minus_one);

// The motion of one pixel in viewport pixels, previous minus current (the sign the upscaler is expected to take; not
// checked against FSR 2). (x, y): the pixel centre, y down; z: NDC z; flip_y: the translator negated clip y, so the rows
// grow with the game's y. Mirrors the motion pass's shader (vulkan/shaders.c motion.frag).
void temporal_motion_px(const double m[16], double vw, double vh, bool flip_y, double x, double y, double z, double out[2]);

// A camera cut: the view direction turned more than 60 degrees since the last frame. Only the turn is checked, so a
// camera that moves without turning is not a cut here. view: row-major view matrices (u[0..15]).
bool temporal_camera_cut(const float view_prev[16], const float view_cur[16]);

// The motion of a pixel with this frame's and the previous frame's jitter taken out, in render pixels.
void temporal_unjitter_motion(const float motion_px[2], float jx_cur, float jy_cur,
                              float jx_prev, float jy_prev, float out[2]);
