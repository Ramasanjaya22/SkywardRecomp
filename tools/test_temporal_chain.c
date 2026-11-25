// The temporal chain around a fake engine: history reset, restart and the jitter fed to an engine. No game, no window.
// Build + run: tools/test_temporal_chain.py.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../runtime/nvn/temporal.h"
#include "../runtime/nvn/temporal_chain.h"

typedef struct FakeEngine {
    int starts, prepares, evaluates, releases;
    char trace[64];
    int trace_len;
    bool fail_next_start;
    TemporalInputs last;
    uint32_t sw, sh, dw, dh;
    bool hdr;
} FakeEngine;

static void record(FakeEngine *f, char c)
{
    f->trace[f->trace_len++] = c;
    f->trace[f->trace_len] = '\0';
}

static bool fake_start(void *ctx, uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool hdr)
{
    FakeEngine *f = ctx;
    record(f, 'S');
    f->starts++;
    f->sw = rw;
    f->sh = rh;
    f->dw = dw;
    f->dh = dh;
    f->hdr = hdr;
    if (f->fail_next_start) {
        f->fail_next_start = false;
        return false;
    }
    return true;
}

static bool fake_prepare(void *ctx, const TemporalInputs *in)
{
    FakeEngine *f = ctx;
    record(f, 'P');
    f->prepares++;
    f->last = *in;
    return true;
}

static bool fake_evaluate(void *ctx, const TemporalInputs *in)
{
    FakeEngine *f = ctx;
    record(f, 'E');
    f->evaluates++;
    f->last = *in;
    return true;
}

static void fake_release(void *ctx)
{
    FakeEngine *f = ctx;
    record(f, 'R');
    f->releases++;
}

static TemporalEngine fake_vtable(FakeEngine *f)
{
    TemporalEngine e;
    memset(&e, 0, sizeof(e));
    e.name = "fake";
    e.start = fake_start;
    e.prepare = fake_prepare;
    e.evaluate = fake_evaluate;
    e.release = fake_release;
    e.ctx = f;
    return e;
}

// Row-major view turned by yaw about y, as temporal_camera_cut expects (u[8..10] is the view direction).
static void yaw_view(double degrees, float v[16])
{
    const double yaw = degrees * 3.14159265358979323846 / 180;
    memset(v, 0, 16 * sizeof(float));
    v[0] = (float)cos(yaw);
    v[2] = (float)sin(yaw);
    v[5] = 1.0f;
    v[8] = -(float)sin(yaw);
    v[10] = (float)cos(yaw);
    v[15] = 1.0f;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-6f;
}

int main(void)
{
    TemporalChain chain;
    TemporalInputs in;

    temporal_chain_init(&chain, NULL);
    assert(!temporal_chain_frame(&chain, 1280, 720, 2560, 1440, false, false, NULL, 16.0f, &in));
    temporal_chain_release(&chain);
    printf("  null engine: ok\n");

    FakeEngine f;
    memset(&f, 0, sizeof(f));
    const TemporalEngine e = fake_vtable(&f);
    temporal_chain_init(&chain, &e);
    float v0[16], v10[16], v90[16], v100[16];
    yaw_view(0, v0);
    yaw_view(10, v10);
    yaw_view(90, v90);
    yaw_view(100, v100);

    assert(temporal_chain_frame(&chain, 1280, 720, 2560, 1440, false, false, v0, 16.0f, &in));
    assert(f.starts == 1 && in.reset && in.phases == 32 && f.prepares == 1 && f.evaluates == 1);
    float ex, ey;
    temporal_jitter(0, 32, &ex, &ey);
    assert(near(in.jitter_x, ex) && near(in.jitter_y, ey));
    assert(temporal_chain_frame(&chain, 1280, 720, 2560, 1440, false, false, v0, 16.0f, &in));
    assert(f.starts == 1 && !in.reset && f.prepares == 2 && f.evaluates == 2);
    temporal_jitter(1, 32, &ex, &ey);
    assert(near(in.jitter_x, ex) && near(in.jitter_y, ey));
    assert(!near(f.last.jitter_x, 0.0f) || !near(f.last.jitter_y, 0.0f));
    printf("  first frames and phases: ok\n");

    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, false, false, v0, 17.0f, &in));
    assert(f.starts == 2 && f.releases == 1 && in.reset && f.sw == 1600 && f.sh == 900 && f.dw == 3200);
    printf("  size change restarts: ok\n");

    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, v0, 17.0f, &in));
    assert(f.starts == 3 && f.releases == 2 && in.reset && in.hdr && f.hdr && f.last.hdr);
    printf("  hdr change restarts: ok\n");

    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, v0, 17.0f, &in));
    assert(!in.reset && f.starts == 3);
    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, v10, 17.0f, &in));
    assert(!in.reset && f.starts == 3);
    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, v90, 17.0f, &in));
    assert(in.reset && f.starts == 3);       // a cut drops history but does not rebuild the engine
    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, NULL, 17.0f, &in));
    assert(!in.reset && f.starts == 3);
    assert(temporal_chain_frame(&chain, 1600, 900, 3200, 1800, true, false, v100, 17.0f, &in));
    assert(!in.reset && f.starts == 3);      // 10 degrees from the last non-NULL view (90), not from identity
    printf("  camera cut and NULL view: ok\n");

    FakeEngine f2;
    memset(&f2, 0, sizeof(f2));
    f2.fail_next_start = true;
    const TemporalEngine e2 = fake_vtable(&f2);
    TemporalChain chain2;
    temporal_chain_init(&chain2, &e2);
    assert(!temporal_chain_frame(&chain2, 1280, 720, 2560, 1440, false, false, NULL, 16.0f, &in));
    assert(f2.starts == 1 && f2.evaluates == 0);
    assert(temporal_chain_frame(&chain2, 1280, 720, 2560, 1440, false, false, v0, 16.0f, &in));
    assert(f2.starts == 2 && f2.prepares == 1 && f2.evaluates == 1);
    float ex0, ey0;
    temporal_jitter(0, temporal_phase_count(1280, 2560), &ex0, &ey0);
    assert(near(f2.last.jitter_x, ex0) && near(f2.last.jitter_y, ey0));
    assert(strcmp(f2.trace, "SRSPE") == 0);  // release precedes the retry; no evaluate after the failed start
    temporal_chain_release(&chain2);
    assert(f2.releases == 2);
    printf("  failed start retries: ok\n");

    const float j_cur[2] = { 0.25f, -0.25f }, j_prev[2] = { -0.25f, 0.25f };
    const float jitter_motion[2] = { j_prev[0] - j_cur[0], j_prev[1] - j_cur[1] };
    float clean[2];
    temporal_unjitter_motion(jitter_motion, j_cur[0], j_cur[1], j_prev[0], j_prev[1], clean);
    assert(near(clean[0], 0.0f) && near(clean[1], 0.0f));
    const float real_motion[2] = { jitter_motion[0] + 3.0f, jitter_motion[1] - 2.0f };
    temporal_unjitter_motion(real_motion, j_cur[0], j_cur[1], j_prev[0], j_prev[1], clean);
    assert(near(clean[0], 3.0f) && near(clean[1], -2.0f));
    printf("  unjitter: ok\n");

    temporal_chain_release(&chain);
    printf("test_temporal_chain: ok\n");
    return 0;
}
