// When the Vulkan pipeline cache is written to disk while the game runs (tools/test_pcache.c). A crash or a killed
// process used to lose everything since the last 3-minute save (a log with "loaded 52 bytes" after a long session).
#pragma once
#include <stddef.h>

#define PCACHE_EMPTY_BYTES 64           // a cache with no pipeline in it is its header only (52 bytes)
#define PCACHE_SAVE_GROWTH (1u << 20)   // this much new: save now, whatever the time
#define PCACHE_SAVE_SECS 60.0           // anything new: save once this long since the last save

static inline int pcache_should_save(size_t now, size_t saved, double secs_since) {
    if (now <= PCACHE_EMPTY_BYTES || now <= saved) return 0;
    return now - saved >= PCACHE_SAVE_GROWTH || secs_since >= PCACHE_SAVE_SECS;
}
