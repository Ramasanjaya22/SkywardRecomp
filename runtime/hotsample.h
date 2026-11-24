// Hot-function sampler (Windows): LARECOMP_HOTLIST_OUT=<file> samples where the game's threads are every millisecond and,
// at exit, writes the hottest generated functions as "0x<guest address> <samples>" lines (tools/gen.py --hot reads
// them). LARECOMP_HOTLIST_N caps the list (default 400). Without the variable, and on other platforms, nothing runs.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "gamelib.h"

// The index of the last start <= pc in the sorted array (-1 below the first start or without any).
ptrdiff_t hot_bin(const uintptr_t *starts, size_t n, uintptr_t pc);
// Writes the hottest entries (count > 0, keep[i] set) as "0x<guest> <count>" lines, highest first, at most `limit`, through
// <path>.tmp renamed over <path> (a reader never sees half a file). Returns the lines written, -1 when it failed.
long hot_write(const char *path, const uint64_t *guest, const uint32_t *count, const unsigned char *keep, size_t n, size_t limit);
// A guest thread to sample (keeps its own copy of the handle).
void hotsample_thread(uintptr_t handle);
// Starts the sampler over the loaded game code's functions.
void hotsample_start(const GameLibTables *t);
