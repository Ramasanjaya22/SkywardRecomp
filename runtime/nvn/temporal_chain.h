#ifndef SKYWARD_TEMPORAL_CHAIN_H
#define SKYWARD_TEMPORAL_CHAIN_H

#include "temporal.h"

#include <stdbool.h>
#include <stdint.h>

/* One frame's inputs, shared by the chain and the engine it drives. */
typedef struct TemporalInputs {
    uint32_t render_w, render_h, display_w, display_h;
    float jitter_x, jitter_y;  // this frame's offset in render pixels (temporal_jitter)
    uint32_t phases;           // temporal_phase_count
    bool reset;                // history must be dropped this frame
    bool depth_reversed, hdr;
    float frame_ms;            // time since the previous frame
} TemporalInputs;

/* A future FSR engine fills this vtable; prepare may be NULL. */
typedef struct TemporalEngine {
    const char *name;                                          // "fsr 4", "fsr 3.1"
    bool (*start)(void *ctx, uint32_t render_w, uint32_t render_h, uint32_t display_w, uint32_t display_h, bool hdr);
    bool (*prepare)(void *ctx, const TemporalInputs *in);      // before the frame records (optional)
    bool (*evaluate)(void *ctx, const TemporalInputs *in);     // records the upscale
    void (*release)(void *ctx);
    void *ctx;
} TemporalEngine;

/* engine pointer, frame counter, sizes, previous view[16], has_prev and started state. */
typedef struct TemporalChain {
    const TemporalEngine *engine;
    uint64_t frame;
    uint32_t render_w, render_h, display_w, display_h;
    float view_prev[16];
    bool has_prev;
    bool started;
    bool hdr;
} TemporalChain;

void temporal_chain_init(TemporalChain *c, const TemporalEngine *e); // e may be NULL: the chain does nothing

// Fills out for this frame and runs the engine (start on first use or size/hdr change, then prepare+evaluate).
// Returns false when there is no engine or it failed (the caller keeps its current upscaler).
bool temporal_chain_frame(TemporalChain *c, uint32_t render_w, uint32_t render_h, uint32_t display_w,
                          uint32_t display_h, bool hdr, bool depth_reversed, const float view[16], float frame_ms,
                          TemporalInputs *out);
void temporal_chain_release(TemporalChain *c);

#endif
