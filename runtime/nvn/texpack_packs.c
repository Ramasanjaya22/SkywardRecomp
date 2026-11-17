// User-supplied texture packs. Identity and filenames follow Kenji-NX (MIT, see README.md);
// no pack files, game assets or generated art are stored in this repository.
#include "texpack_packs.h"
#include "../paths.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_PACKS 4096
#define PACK_NAME_LEN 256
#define PACK_PATH_LEN 1024

typedef struct { char name[PACK_NAME_LEN]; char path[PACK_PATH_LEN]; } PackFile;

static PackFile *g_packs;
static int g_npacks;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int cmp_pack(const void *a, const void *b) {
    return strcmp(((const PackFile *)a)->name, ((const PackFile *)b)->name);
}

static void scan_packs(void) {
    char dir[PACK_PATH_LEN];
    snprintf(dir, sizeof dir, "%s/textures", rt_data_root());
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "[texpack] no pack folder at %s\n", dir);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 5 || len >= PACK_NAME_LEN) continue;
        const char *ext = e->d_name + len - 4;
        if (strcasecmp(ext, ".dds") && strcasecmp(ext, ".png")) continue;
        if (g_npacks >= MAX_PACKS) break;
        PackFile *p = &g_packs[g_npacks];
        snprintf(p->name, sizeof p->name, "%s", e->d_name);
        snprintf(p->path, sizeof p->path, "%s/%s", dir, e->d_name);
        g_npacks++;
    }
    closedir(d);
    qsort(g_packs, (size_t)g_npacks, sizeof *g_packs, cmp_pack);
    fprintf(stderr, "[texpack] %d pack file(s) in %s\n", g_npacks, dir);
}

void texpack_packs_init(void) {
    if (!g_packs) g_packs = (PackFile *)calloc(MAX_PACKS, sizeof *g_packs);
    if (!g_packs) return;
    pthread_once(&g_once, scan_packs);
}

bool texpack_packs_enabled(void) { texpack_packs_init(); return g_npacks > 0; }

void texpack_packs_name(char *out, size_t n, uint32_t target,
                        const uint64_t hash[2], uint32_t w, uint32_t h, uint32_t d) {
    const char *prefix = "tex";
    switch (target) {
    case 1:         prefix = "tex2d";    break;  // NVN 2D
    case 2:         prefix = "tex3d";    break;  // NVN 3D
    case 8:         prefix = "texcube";  break;  // NVN cubemap
    case 4: case 6: prefix = "texa2d";   break;  // NVN 2D array
    case 9:         prefix = "texacube"; break;  // NVN cubemap array
    }
    snprintf(out, n, "%s_%016llx%016llx_%ux%ux%u.dds", prefix,
             (unsigned long long)hash[0], (unsigned long long)hash[1],
             (unsigned)w, (unsigned)h, (unsigned)d);
}

const char *texpack_packs_find(const char *name) {
    texpack_packs_init();
    if (!g_packs || !name) return NULL;
    int lo = 0, hi = g_npacks - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(g_packs[mid].name, name);
        if (c == 0) return g_packs[mid].path;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t bc_of_fourcc(const uint8_t *p) {
    if (!memcmp(p, "DXT1", 4)) return 1;
    if (!memcmp(p, "DXT3", 4) || !memcmp(p, "DXT5", 4)) return 3;
    if (!memcmp(p, "ATI1", 4)) return 5;
    if (!memcmp(p, "ATI2", 4) || !memcmp(p, "BC5U", 4)) return 5;
    return 0;
}

static uint8_t bc_of_dxgi(uint32_t f) {
    switch (f) {
    case 71: case 72: return 1;
    case 74: case 75: case 77: case 78: return 3;
    case 80: case 81: case 82: case 83: case 84: case 85: return 5;
    case 98: case 99: return 7;
    }
    return 0;
}

// Minimal PNG reader for the user's own texture packs: 8-bit RGB/RGBA, non-interlaced.
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

bool texpack_packs_png_load(const char *path, TexPackPng *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long flen = ftell(f);
    if (flen < 64 || flen > (256 << 20)) { fclose(f); return false; }
    rewind(f);
    uint8_t *file = (uint8_t *)malloc((size_t)flen);
    if (!file) { fclose(f); return false; }
    size_t nread = fread(file, 1, (size_t)flen, f);
    fclose(f);
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (nread != (size_t)flen || memcmp(file, sig, 8)) { free(file); return false; }

    uint32_t w = 0, h = 0;
    int bpp = 0;
    uint8_t *idat = NULL;
    size_t idat_n = 0, idat_cap = 0;
    bool ok = false;
    size_t pos = 8;
    while (pos + 12 <= (size_t)flen) {
        uint32_t len = be32(file + pos);
        const uint8_t *type = file + pos + 4;
        const uint8_t *data = file + pos + 8;
        if ((uint64_t)len + 12 > (size_t)flen - pos) break;
        if (!memcmp(type, "IHDR", 4)) {
            if (len < 13) break;
            w = be32(data); h = be32(data + 4);
            int depth = data[8], colour = data[9], interlace = data[12];
            if (depth != 8 || interlace != 0 || (colour != 2 && colour != 6) || !w || !h || w > 16384 || h > 16384) break;
            bpp = colour == 6 ? 4 : 3;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (idat_n + len > idat_cap) {
                size_t cap = idat_n + len + (1 << 20);
                uint8_t *tmp = (uint8_t *)realloc(idat, cap);
                if (!tmp) break;
                idat = tmp; idat_cap = cap;
            }
            memcpy(idat + idat_n, data, len);
            idat_n += len;
        } else if (!memcmp(type, "IEND", 4)) {
            ok = w && h && bpp && idat_n;
            break;
        }
        pos += (size_t)len + 12;
    }
    free(file);
    if (!ok) { free(idat); return false; }

    size_t stride = (size_t)w * bpp;
    size_t raw_n = (stride + 1) * h;
    uint8_t *raw = (uint8_t *)malloc(raw_n);
    if (!raw) { free(idat); return false; }
    uLongf dst = (uLongf)raw_n;
    int zr = uncompress(raw, &dst, idat, (uLong)idat_n);
    free(idat);
    if (zr != Z_OK || dst != raw_n) { free(raw); return false; }

    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!rgba) { free(raw); return false; }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (stride + 1);
        int ft = row[0];
        for (size_t x = 0; x < stride; x++) {
            uint8_t a = x >= (size_t)bpp ? row[x - bpp] : 0;
            uint8_t b = y ? raw[(size_t)(y - 1) * (stride + 1) + 1 + x] : 0;
            uint8_t c = (y && x >= (size_t)bpp) ? raw[(size_t)(y - 1) * (stride + 1) + 1 + x - bpp] : 0;
            uint8_t v = row[1 + x];
            if (ft == 1) v = (uint8_t)(v + a);
            else if (ft == 2) v = (uint8_t)(v + b);
            else if (ft == 3) v = (uint8_t)(v + ((a + b) >> 1));
            else if (ft == 4) v = (uint8_t)(v + paeth(a, b, c));
            else if (ft != 0) { free(raw); free(rgba); return false; }
            row[1 + x] = v;
        }
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *src = row + 1 + (size_t)x * bpp;
            uint8_t *dstp = rgba + ((size_t)y * w + x) * 4;
            dstp[0] = src[0]; dstp[1] = src[1]; dstp[2] = src[2];
            dstp[3] = bpp == 4 ? src[3] : 255;
        }
    }
    free(raw);
    out->rgba = rgba; out->w = w; out->h = h;
    return true;
}

void texpack_packs_png_free(TexPackPng *p) {
    if (!p) return;
    free(p->rgba);
    memset(p, 0, sizeof *p);
}

const uint8_t *texpack_packs_png_sample(const TexPackPng *p, uint32_t w, uint32_t h) {
    if (!p || !p->rgba || !w || !h) return NULL;
    static uint8_t *px;
    static size_t cap;
    size_t need = (size_t)w * h * 4;
    if (need > cap) {
        uint8_t *tmp = (uint8_t *)realloc(px, need);
        if (!tmp) return NULL;
        px = tmp; cap = need;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * p->h / h);
        for (uint32_t x = 0; x < w; x++) {
            uint32_t sx = (uint32_t)((uint64_t)x * p->w / w);
            memcpy(px + ((size_t)y * w + x) * 4, p->rgba + ((size_t)sy * p->w + sx) * 4, 4);
        }
    }
    return px;
}

bool texpack_packs_dds_load(const char *path, TexPackDds *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[148];
    size_t got = fread(h, 1, sizeof h, f);
    if (got < 128 || memcmp(h, "DDS ", 4) || rd32(h + 4) != 124) { fclose(f); return false; }

    uint32_t w = rd32(h + 16), hh = rd32(h + 12);
    uint32_t levels = (rd32(h + 8) & 0x20000) ? rd32(h + 28) : 1;
    if (!w || !hh || w > 16384 || hh > 16384 || !levels || levels > 15) { fclose(f); return false; }

    uint8_t bc = 0;
    size_t data_off = 128;
    if (!memcmp(h + 84, "DX10", 4)) {
        if (got < 148) { fclose(f); return false; }
        bc = bc_of_dxgi(rd32(h + 128));
        data_off = 148;
    } else {
        bc = bc_of_fourcc(h + 84);
    }
    if (bc != 1 && bc != 3 && bc != 5 && bc != 7) { fclose(f); return false; }
    uint8_t bpb = (bc == 1) ? 8 : 16;

    size_t need = 0;
    for (uint32_t lv = 0, lw = w, lh = hh; lv < levels; lv++) {
        need += (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * bpb;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    if (fseek(f, (long)data_off, SEEK_SET)) { fclose(f); return false; }
    uint8_t *data = (uint8_t *)malloc(need);
    if (!data) { fclose(f); return false; }
    size_t rd = fread(data, 1, need, f);
    if (rd != need) { free(data); fclose(f); return false; }
    fclose(f);

    out->data = data;
    out->size = need;
    out->w = w; out->h = hh;
    uint32_t dep = rd32(h + 24);
    out->d = dep ? dep : 1;
    out->levels = levels;
    out->bpb = bpb; out->bc = bc;
    return true;
}

void texpack_packs_dds_free(TexPackDds *p) {
    if (!p) return;
    free(p->data);
    memset(p, 0, sizeof *p);
}

bool texpack_packs_dds_level(const TexPackDds *p, uint32_t lv,
                             const uint8_t **data, size_t *bytes, uint32_t *w, uint32_t *h) {
    if (!p || !p->data || lv >= p->levels) return false;
    uint32_t lw = p->w, lh = p->h;
    size_t off = 0;
    for (uint32_t i = 0; i < lv; i++) {
        off += (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * p->bpb;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    size_t n = (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * p->bpb;
    if (off + n > p->size) return false;
    *data = p->data + off; *bytes = n; *w = lw; *h = lh;
    return true;
}