// Unit test for the texture-pack filename builder and DDS parser (no game data).
#include "runtime/nvn/texpack_packs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// Stub for the unit test: the production binary links runtime/paths.c.
const char *rt_data_root(void) { return "."; }

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static bool write_test_dds(const char *path) {
    uint8_t h[136];
    memset(h, 0, sizeof h);
    memcpy(h, "DDS ", 4);
    put32(h + 4, 124);
    put32(h + 4, 124);                 // dwSize (after the 4-byte magic)
    put32(h + 8, 0x1000 | 0x20000);  // dwFlags: pixel format + mip count
    put32(h + 12, 4); put32(h + 16, 4);  // dwHeight, dwWidth
    put32(h + 28, 1);  // dwMipMapCount
    put32(h + 76, 32);
    memcpy(h + 84, "DXT1", 4);
    put32(h + 108, 0x1000);  // DDSCAPS_TEXTURE
    memset(h + 128, 0x11, 8);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    fclose(f);
    return ok;
}

static void putbe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void append_chunk(uint8_t *out, size_t *n, const char *type, const uint8_t *data, uint32_t len) {
    putbe32(out + *n, len);
    memcpy(out + *n + 4, type, 4);
    if (len) memcpy(out + *n + 8, data, len);
    putbe32(out + *n + 8 + len, 0);  // CRC not checked by our reader
    *n += (size_t)len + 12;
}
static bool write_test_png(const char *path) {
    const uint8_t ihdr[13] = {0,0,0,2, 0,0,0,2, 8,6, 0,0,0};
    uint8_t raw[2 * (1 + 2 * 4)];
    for (int y = 0; y < 2; y++) {
        raw[y * 9] = 0;
        for (int x = 0; x < 2; x++) {
            uint8_t *p = raw + y * 9 + 1 + x * 4;
            p[0] = (uint8_t)(10 + 40 * x); p[1] = (uint8_t)(20 + 50 * y);
            p[2] = (uint8_t)(30 + x + y); p[3] = 255;
        }
    }
    uLongf zcap = compressBound(sizeof raw);
    uint8_t *z = (uint8_t *)malloc(zcap);
    if (compress2(z, &zcap, raw, sizeof raw, 6) != Z_OK) { free(z); return false; }
    uint8_t out[512];
    size_t n = 0;
    memcpy(out, "\x89PNG\r\n\x1a\n", 8); n = 8;
    append_chunk(out, &n, "IHDR", ihdr, 13);
    append_chunk(out, &n, "IDAT", z, (uint32_t)zcap);
    append_chunk(out, &n, "IEND", NULL, 0);
    free(z);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(out, 1, n, f) == n;
    fclose(f);
    return ok;
}

int main(void) {
    int fail = 0;
    char name[256];
    uint64_t hash[2] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
    texpack_packs_name(name, sizeof name, 1, hash, 4, 4, 1);
    const char *want = "tex2d_0123456789abcdeffedcba9876543210_4x4x1.dds";
    if (strcmp(name, want)) { printf("FAIL name: %s\n", name); fail++; }
    texpack_packs_name(name, sizeof name, 4, hash, 8, 8, 2);
    if (strncmp(name, "texa2d_", 7)) { printf("FAIL array prefix: %s\n", name); fail++; }

    const char *path = "texpack_packs_test.dds";
    if (!write_test_dds(path)) { printf("FAIL write dds\n"); return 1; }
    TexPackDds p;
    if (!texpack_packs_dds_load(path, &p)) { printf("FAIL load dds\n"); fail++; }
    else {
        if (p.w != 4 || p.h != 4 || p.d != 1 || p.levels != 1 || p.bpb != 8 || p.bc != 1 || p.size != 8) {
            printf("FAIL dds fields: %ux%ux%u lv=%u bpb=%u bc=%u size=%zu\n",
                   p.w, p.h, p.d, p.levels, p.bpb, p.bc, p.size);
            fail++;
        }
        const uint8_t *lv = NULL; size_t bytes = 0; uint32_t lw = 0, lh = 0;
        if (!texpack_packs_dds_level(&p, 0, &lv, &bytes, &lw, &lh) || bytes != 8 || lw != 4 || lh != 4 || lv[0] != 0x11) {
            printf("FAIL dds level\n"); fail++;
        }
        if (texpack_packs_dds_level(&p, 1, &lv, &bytes, &lw, &lh)) { printf("FAIL missing level accepted\n"); fail++; }
        texpack_packs_dds_free(&p);
    }
    const char *ppath = "texpack_packs_test.png";
    if (!write_test_png(ppath)) { printf("FAIL write png\n"); fail++; }
    else {
        TexPackPng png;
        if (!texpack_packs_png_load(ppath, &png)) { printf("FAIL load png\n"); fail++; }
        else {
            if (png.w != 2 || png.h != 2 || png.rgba[0] != 10 || png.rgba[1] != 20 || png.rgba[4] != 50) {
                printf("FAIL png fields: %ux%u first=%u,%u,%u,%u row1=%u\n", png.w, png.h,
                       png.rgba[0], png.rgba[1], png.rgba[2], png.rgba[3], png.rgba[4]);
                fail++;
            }
            const uint8_t *s = texpack_packs_png_sample(&png, 1, 1);
            if (!s || s[0] != 10 || s[1] != 20) { printf("FAIL png sample\n"); fail++; }
            texpack_packs_png_free(&png);
        }
        remove(ppath);
    }
    remove(path);
    printf(fail ? "FAIL (%d)\n" : "PASS\n", fail);
    return fail ? 1 : 0;
}