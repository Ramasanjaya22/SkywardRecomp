// The [perf] summary line: one per 10 s (LARECOMP_PERF=0: off) from the frames pace_frame measured, so a run's numbers
// (fps, frame time percentiles, GPU, memory) can be compared before and after a change, and against another emulator's
// Task Manager column (private working set).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perfstat.h"
#include "rt.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

bool perfstat_mem(uint64_t *ws, uint64_t *priv, uint64_t *priv_ws) {
#ifdef _WIN32
    struct { PROCESS_MEMORY_COUNTERS_EX base; SIZE_T private_working_set, shared_commit; } m;  // (= PROCESS_MEMORY_COUNTERS_EX2, Win10: its size is what asks for the private working set)
    memset(&m, 0, sizeof m);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof m)) return false;
    *ws = m.base.WorkingSetSize, *priv = m.base.PrivateUsage, *priv_ws = m.private_working_set;
    if (!*priv_ws || *priv_ws > *ws) *priv_ws = *ws;  // (older Windows: no private working set)
    return true;
#elif defined(__APPLE__)
    struct task_vm_info v;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&v, &n) != KERN_SUCCESS) return false;
    *ws = v.resident_size, *priv = *priv_ws = v.phys_footprint;
    if (*priv_ws > *ws) *ws = *priv_ws;
    return true;
#else
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[256];
    unsigned long long rss = 0, anon = 0;
    while (fgets(line, sizeof line, f)) {
        sscanf(line, "VmRSS: %llu", &rss);
        sscanf(line, "RssAnon: %llu", &anon);
    }
    fclose(f);
    *ws = rss * 1024, *priv = *priv_ws = anon * 1024;
    if (*priv_ws > *ws) *ws = *priv_ws;
    return rss > 0;
#endif
}

static int cmp_f(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

float perfstat_pct(const float *v, int n, float p) {
    if (n <= 0) return 0;
    float s[600];
    if (n > 600) n = 600;
    memcpy(s, v, (size_t)n * sizeof *s);
    qsort(s, (size_t)n, sizeof *s, cmp_f);
    float r = p / 100 * (float)(n - 1);
    int i = (int)r;
    return i >= n - 1 ? s[n - 1] : s[i] + (s[i + 1] - s[i]) * (r - (float)i);
}

#define N 600
int perfstat_frame(float busy_ms, float game_ms, float rq_wait_ms, float render_cpu_ms, float gpu_ms) {
    static int off = -1;
    if (off < 0) { const char *e = getenv("LARECOMP_PERF"); off = e && atoi(e) == 0; }
    if (off) return 0;
    static float busy[N], game[N], rq[N], rcpu[N], gpu[N];
    static int idx, filled, ngpu;
    static uint64_t t0, frames;
    uint64_t now = plat_now_ns();
    if (!t0) t0 = now;
    busy[idx] = busy_ms, game[idx] = game_ms, rq[idx] = rq_wait_ms, rcpu[idx] = render_cpu_ms;
    if (gpu_ms >= 0) gpu[ngpu++ % N] = gpu_ms;
    idx = (idx + 1) % N, filled = filled < N ? filled + 1 : N, frames++;
    if (now - t0 < 10000000000ull) return 0;
    double secs = (double)(now - t0) / 1e9;
    char g[64] = "gpu not timed";
    if (ngpu) {
        int m = ngpu < N ? ngpu : N;
        snprintf(g, sizeof g, "gpu p50 %.1f p95 %.1f", perfstat_pct(gpu, m, 50), perfstat_pct(gpu, m, 95));
    }
    uint64_t ws = 0, priv = 0, pws = 0;
    char mem[96] = "mem n/a";
    if (perfstat_mem(&ws, &priv, &pws))
        snprintf(mem, sizeof mem, "mem ws %llu priv %llu privws %llu MB", (unsigned long long)(ws >> 20),
                 (unsigned long long)(priv >> 20), (unsigned long long)(pws >> 20));
    LOG("[perf] fps %.1f | busy p50 %.1f p95 %.1f | game p50 %.1f | rq wait p50 %.1f | render cpu p50 %.1f | %s ms | %s\n",
        (double)frames / secs, perfstat_pct(busy, filled, 50), perfstat_pct(busy, filled, 95), perfstat_pct(game, filled, 50),
        perfstat_pct(rq, filled, 50), perfstat_pct(rcpu, filled, 50), g, mem);
    t0 = now, frames = 0, ngpu = 0;
    return 1;
}
