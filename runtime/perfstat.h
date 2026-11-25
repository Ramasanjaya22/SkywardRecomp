// The [perf] summary line (runtime/perfstat.c): frame time percentiles and process memory, every 10 s.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Once per presented frame (ms): busy, the game thread's share (busy less the wait for the render thread), that wait,
// the render thread's CPU time and the GPU time (-1: not timed). Returns 1 when it logged the summary.
int perfstat_frame(float busy_ms, float game_ms, float rq_wait_ms, float render_cpu_ms, float gpu_ms);
// This process's memory in bytes, as Task Manager's columns: the working set, private bytes (committed) and the private
// working set (the "Memory" column). false: not available here.
bool perfstat_mem(uint64_t *ws, uint64_t *priv, uint64_t *priv_ws);
// The p-th percentile (0-100, linear interpolation) of v[0..n); v is left as it is. 0 for n == 0.
float perfstat_pct(const float *v, int n, float p);
