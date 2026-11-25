#include "temporal_chain.h"

#include <string.h>

void temporal_chain_init(TemporalChain *c, const TemporalEngine *e)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->engine = e;
}

void temporal_chain_release(TemporalChain *c)
{
    if (c && c->started && c->engine && c->engine->release) {
        c->engine->release(c->engine->ctx);
    }
    if (c) {
        c->started = false;
    }
}

bool temporal_chain_frame(TemporalChain *c, uint32_t render_w, uint32_t render_h, uint32_t display_w,
                          uint32_t display_h, bool hdr, bool depth_reversed, const float view[16], float frame_ms,
                          TemporalInputs *out)
{
    if (!c || !c->engine) {
        return false;
    }

    const TemporalEngine *e = c->engine;
    const uint32_t phases = temporal_phase_count(render_w, display_w);
    const bool new_size = c->frame == 0 || render_w != c->render_w || render_h != c->render_h ||
                          display_w != c->display_w || display_h != c->display_h || hdr != c->hdr;
    const bool cut = view && c->has_prev && temporal_camera_cut(c->view_prev, view);
    const bool reset = new_size || cut;
    const bool restart = new_size; // a camera cut only drops history; size/hdr changes rebuild the engine

    if (view) {
        memcpy(c->view_prev, view, sizeof(c->view_prev));
        c->has_prev = true;
    }
    TemporalInputs in;
    memset(&in, 0, sizeof(in));
    in.render_w = render_w;
    in.render_h = render_h;
    in.display_w = display_w;
    in.display_h = display_h;
    temporal_jitter((uint32_t)c->frame, phases, &in.jitter_x, &in.jitter_y);
    in.phases = phases;
    in.reset = reset;
    in.depth_reversed = depth_reversed;
    in.hdr = hdr;
    in.frame_ms = frame_ms;
    if (out) {
        *out = in;
    }

    if (restart) {
        temporal_chain_release(c);
    }
    if (!c->started) {
        if (!e->start || !e->start(e->ctx, render_w, render_h, display_w, display_h, hdr)) {
            // A failed start may have taken engine resources: give them back before the next attempt.
            if (e->release) {
                e->release(e->ctx);
            }
            return false;
        }
        c->started = true;
    }
    // Commit only after a successful start so a failed start retries with the
    // same size and jitter phase.
    c->frame++;
    c->render_w = render_w;
    c->render_h = render_h;
    c->display_w = display_w;
    c->display_h = display_h;
    c->hdr = hdr;
    if (e->prepare && !e->prepare(e->ctx, &in)) {
        return false;
    }
    return !e->evaluate || e->evaluate(e->ctx, &in);
}
