// hot_bin (runtime/hotsample.c): the function a program counter falls in. Built with -DHOTSAMPLE_TEST by test_hotsample.py.
#include <stdio.h>
#include <string.h>

#include "hotsample.h"

static int bad;
#define CHECK(expr, want) do { long long got = (expr); if (got != (want)) { printf("FAIL %s = %lld, want %lld\n", #expr, got, (long long)(want)); bad++; } } while (0)

int main(int argc, char **argv) {
    const uintptr_t s[] = {0x1000, 0x2000, 0x3000};
    CHECK(hot_bin(s, 3, 0x0fff), -1);  // below the first function
    CHECK(hot_bin(s, 3, 0x1000), 0);
    CHECK(hot_bin(s, 3, 0x2fff), 1);
    CHECK(hot_bin(s, 3, 0x3000), 2);
    CHECK(hot_bin(s, 3, 0x9999), 2);   // past the last start: still the last function (the caller bounds the module)
    CHECK(hot_bin(s, 0, 0x1000), -1);  // no functions
    CHECK(hot_bin(s, 1, 0x1fff), 0);
    // hot_write: the list file the sampler leaves (C1: written while the game runs, not only at a clean exit)
    char path[512];  // in the directory test_hotsample.py passes
    snprintf(path, sizeof path, "%s/hotsample-test.out", argc > 1 ? argv[1] : ".");
    const uint64_t g[] = {0xa, 0xb, 0xc, 0xd};
    const uint32_t c[] = {5, 0, 9, 7};
    const unsigned char keep[] = {1, 1, 1, 0};  // 0xd is no function of the main code: not listed (I2)
    char buf[256] = {0};
    CHECK(hot_write(path, g, c, keep, 4, 10), 2);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, 1);
    if (f) { fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    CHECK(strcmp(buf, "0xc 9\n0xa 5\n"), 0);
    CHECK(hot_write(path, g, c, keep, 4, 1), 1);  // the limit
    f = fopen(path, "r");
    memset(buf, 0, sizeof buf);
    if (f) { fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    CHECK(strcmp(buf, "0xc 9\n"), 0);
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "r");
    CHECK(f == NULL, 1);  // the temporary file is renamed away
    if (f) fclose(f);
    CHECK(hot_write("no-such-dir/x.out", g, c, keep, 4, 10), -1);
    remove(path);
    printf(bad ? "%d hot cases failed\n" : "hot_bin, hot_write: ok\n", bad);
    return bad != 0;
}
