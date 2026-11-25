// [perf] summary (runtime/perfstat.c): percentiles and process memory. tools/test_perfstat.py.
#include <math.h>
#include <stdio.h>

#include "perfstat.h"
#include "rt.h"

static uint64_t t_ns = 1000000000ull;
uint64_t plat_now_ns(void) { return t_ns; }

static int failures;
#define CHECK(c)                                                                                 \
    do {                                                                                         \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
    } while (0)

int main(void) {
    float v[100];
    for (int i = 0; i < 100; i++) v[i] = (float)(100 - i);  // (unsorted input)
    CHECK(fabsf(perfstat_pct(v, 100, 50) - 50.5f) < 1e-3f);
    CHECK(fabsf(perfstat_pct(v, 100, 95) - 95.05f) < 1e-3f);
    CHECK(perfstat_pct(v, 100, 0) == 1.0f && perfstat_pct(v, 100, 100) == 100.0f);
    CHECK(v[0] == 100.0f);  // (the caller's array is left as it was)
    CHECK(perfstat_pct(v, 0, 50) == 0.0f);

    uint64_t ws = 0, priv = 0, privws = 0;
    CHECK(perfstat_mem(&ws, &priv, &privws));
    CHECK(ws > 0 && privws <= ws);

    // a line every 10 s: the window opens at the first frame, so 1201 frames at 60 fps span 20 s -> two summaries
    int lines = 0;
    for (int i = 0; i < 1201; i++) {
        t_ns += 16666667ull;
        lines += perfstat_frame(12.0f, 9.0f, 1.0f, 7.0f, 6.0f);
    }
    CHECK(lines == 2);

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
