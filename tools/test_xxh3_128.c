/*
 * Standalone test for the KenjiNX XXH3-128 texture hash port.
 *
 * Expected values are 32-hex strings in KenjiNX order: high64 then low64.
 * Generated with the official xxHash v0.8.5 reference implementation
 * (XXH3_128bits, seed 0; default secret b8fe6c39...), cross-checked against
 * tests/sanity_test_vectors.h from Cyan4973/xxHash for the empty input.
 * Pattern inputs are data[i] = (unsigned char)(i * 31 + 7).
 */
#include "xxh3_128.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t len;
    const char *expected;
} vector_t;

static unsigned char pattern[2048];

int main(void)
{
    static const unsigned char abc[] = { 'a', 'b', 'c' };
    size_t i;
    int failures = 0;

    for (i = 0; i < sizeof(pattern); i++)
        pattern[i] = (unsigned char)(i * 31u + 7u);

    {
        static const vector_t vectors[] = {
            { "empty",       (const unsigned char *)"", 0,
              "99aa06d3014798d86001c324468d497f" },
            { "abc",         abc, 3,
              "06b05ab6733a618578af5f94892f3950" },
            { "pattern-17",  pattern, 17,
              "18217300b5132d5a78c349fe81b2f26c" },
            { "pattern-129", pattern, 129,
              "6881633650cd8924c51bc887976aef63" },
            { "pattern-200", pattern, 200,
              "8d8629a1aef9ef9060ea018811f9a437" },
            { "pattern-240", pattern, 240,
              "de57aab31e77a2ff93e173833f75ab66" },
            { "pattern-241", pattern, 241,
              "92b991a7192f3f080b3b630948ce4a00" },
            { "pattern-1000", pattern, 1000,
              "f534f51e82a81d29989765d0ea7a5ecd" }
        };
        size_t v;
        for (v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
            uint64_t out[2];
            char got[33];
            xxh3_128(vectors[v].data, vectors[v].len, out);
            sprintf(got, "%016llx%016llx",
                    (unsigned long long)out[0], (unsigned long long)out[1]);
            if (strcmp(got, vectors[v].expected) == 0) {
                printf("PASS %-13s len=%-4zu %s\n", vectors[v].name, vectors[v].len, got);
            } else {
                printf("FAIL %-13s len=%-4zu got=%s expected=%s\n",
                       vectors[v].name, vectors[v].len, got, vectors[v].expected);
                failures++;
            }
        }
    }

    printf("%s (%d failing vectors)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
