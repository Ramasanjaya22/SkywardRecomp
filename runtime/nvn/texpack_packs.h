// User-supplied texture packs in <data root>/textures/ (no pack files ship with the app).
// Filenames follow Kenji-NX (MIT): <prefix>_<xxh3-128 high><low>_<w>x<h>x<d>.dds
// where d = depth * layers and the hash is of the fully linearized guest texture bytes.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *rgba;      // decoded PNG, w * h * 4
    uint32_t w, h;
} TexPackPng;

typedef struct {
    uint8_t *data;      // all mips, tightly packed
    size_t size;
    uint32_t w, h, d;   // top-level dimensions
    uint32_t levels;
    uint8_t bpb;        // bytes per 4x4 block: 8 (BC1) or 16 (BC3/BC5/BC7)
    uint8_t bc;         // 1, 3, 5 or 7
} TexPackDds;

// Scan once. Missing folder is logged once and is not an error.
void texpack_packs_init(void);
// True when at least one .dds file is available (cheap early-out for the upload hook).
bool texpack_packs_enabled(void);

// Build the Kenji-NX filename for a guest texture. hash[0] = high 64 bits, hash[1] = low.
void texpack_packs_name(char *out, size_t n, uint32_t nvn_target,
                        const uint64_t hash[2], uint32_t w, uint32_t h, uint32_t d);

// Returns the full path of a pack file with this exact name, or NULL. Valid until next init.
const char *texpack_packs_find(const char *name);

// Parse a DDS with BC1/BC3/BC5/BC7 blocks. False on any malformed or unsupported file.
bool texpack_packs_png_load(const char *path, TexPackPng *out);
void texpack_packs_png_free(TexPackPng *p);
// Sample a PNG at a smaller mip size (nearest neighbour); returns a pointer valid while p lives.
const uint8_t *texpack_packs_png_sample(const TexPackPng *p, uint32_t w, uint32_t h);

bool texpack_packs_dds_load(const char *path, TexPackDds *out);
void texpack_packs_dds_free(TexPackDds *p);

// Slice one mip out of a parsed DDS. False if the level is missing.
bool texpack_packs_dds_level(const TexPackDds *p, uint32_t lv,
                             const uint8_t **data, size_t *bytes, uint32_t *w, uint32_t *h);

#ifdef __cplusplus
}
#endif