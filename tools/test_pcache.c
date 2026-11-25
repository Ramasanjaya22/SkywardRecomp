// When the Vulkan pipeline cache is written while playing (runtime/nvn/vulkan/pcache_policy.h). tools/test_pcache.py.
#include <stdio.h>

#include "nvn/vulkan/pcache_policy.h"

static int failures;
#define CHECK(c)                                                                                 \
    do {                                                                                         \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
    } while (0)

int main(void) {
    CHECK(!pcache_should_save(52, 0, 0));                  // an empty cache (its 52-byte header) is not worth a file
    CHECK(!pcache_should_save(52, 0, 600));                // not even after a long time
    CHECK(!pcache_should_save(5000000, 5000000, 600));     // nothing new
    CHECK(!pcache_should_save(5000000, 4900000, 59.0));    // a little new, and recently saved
    CHECK(pcache_should_save(5000000, 4900000, 61.0));     // a little new, a minute since the last save
    CHECK(pcache_should_save(1048576, 0, 1.0));            // first MB of a new cache: at once
    CHECK(pcache_should_save(3000000 + 1048576, 3000000, 1.0));  // 1 MB more than the last save: at once
    CHECK(!pcache_should_save(3000000 + 1048575, 3000000, 1.0));
    CHECK(!pcache_should_save(100, 5000000, 600));         // shrank (a driver trimming it): the last save stays
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
