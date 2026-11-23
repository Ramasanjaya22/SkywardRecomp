// Finds the user's game in a folder they picked (runtime/game_find.h).
#include "game_find.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

// Skyward Sword HD (tools/extract.py: GAME_TITLE_ID / UPDATE_TITLE_ID).
#define TITLE_BASE   0x01002DA013484000ull
#define TITLE_UPDATE 0x01002DA013484800ull
#define MAX_DEPTH 4
#define MAX_DIRS 4000  // directory reads: a drive's root can't stall the launcher

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && ieq(name + n - e, ext);
}

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static void note(GameFind *g, const char *path, const char *why) {
    if (g->nnotes >= GAME_FIND_NOTES) { g->nmore++; return; }
    snprintf(g->notes[g->nnotes].path, sizeof g->notes[0].path, "%s", path);
    snprintf(g->notes[g->nnotes].why, sizeof g->notes[0].why, "%s", why);
    g->nnotes++;
}

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const unsigned char *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

static int hex_run(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return 0;
    return 1;
}

// A .nsp is a PFS0 container: "PFS0", file count, string table size, then 24-byte entries (offset, size, name offset), the names, the data.
// Only the table is read (a few KB of a multi-GB file), and it has to fit the file. Its ticket's name is the rights id, whose first 16 hex
// digits are the title id: that tells base game from update, DLC and other games without any key.
int game_nsp_classify(const char *path, char *why, size_t why_size) {
#define BAD(...) do { snprintf(why, why_size, __VA_ARGS__); fclose(f); return GAME_NSP_BAD; } while (0)
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(why, why_size, "can't be opened (permissions, or the file is in use)"); return GAME_NSP_BAD; }
    if (FSEEK64(f, 0, SEEK_END) != 0) BAD("can't be read");
    long long size = (long long)FTELL64(f);
    unsigned char h[16];
    if (FSEEK64(f, 0, SEEK_SET) != 0 || size < 16 || fread(h, 1, 16, f) != 16) BAD("too small to be a game package (%lld bytes): an incomplete download or copy?", size);
    if (memcmp(h, "PFS0", 4)) BAD("not a game package: it doesn't start with a PFS0 header (wrong file type, or damaged)");
    uint32_t count = le32(h + 4), strsz = le32(h + 8);
    if (count == 0) BAD("an empty game package");
    if (count > 4096 || strsz > (1u << 20)) BAD("damaged: the package's file table is out of range");
    long long head = 16 + 24ll * count + strsz;
    if (head > size) BAD("cut short: the file ends inside the package's file table (an incomplete download or copy?)");
    unsigned char *tab = (unsigned char *)malloc((size_t)(head - 16));
    if (!tab) BAD("out of memory");
    if (fread(tab, 1, (size_t)(head - 16), f) != (size_t)(head - 16)) { free(tab); BAD("can't be read"); }
    fclose(f);
    const unsigned char *names = tab + 24ll * count;
    int result = GAME_NSP_UNKNOWN, ncas = 0, ntik = 0;
    uint64_t title = 0;
    for (uint32_t i = 0; i < count; i++) {
        const unsigned char *e = tab + 24ll * i;
        uint64_t off = le64(e), sz = le64(e + 8);
        uint32_t no = le32(e + 16);
        if (no >= strsz || off > (uint64_t)(size - head) || sz > (uint64_t)(size - head) - off) {
            free(tab);
            snprintf(why, why_size, "cut short: the file is smaller than its contents say (an incomplete download or copy?)");
            return GAME_NSP_BAD;
        }
        char name[260];
        size_t n = 0;
        while (no + n < strsz && names[no + n] && n < sizeof name - 1) { name[n] = (char)names[no + n]; n++; }
        name[n] = 0;
        if (has_ext(name, ".nca")) ncas++;
        if (has_ext(name, ".tik") && n >= 36 && hex_run(name, 32)) {
            char id[17];
            memcpy(id, name, 16);
            id[16] = 0;
            title = strtoull(id, NULL, 16);
            ntik++;
        }
    }
    free(tab);
    if (!ncas) { snprintf(why, why_size, "has no game data (no .nca files inside)"); return GAME_NSP_BAD; }
    if (!ntik) {  // (no ticket: the title id in the file name, "[0100...]", is all there is; extraction checks the real one)
        const char *base = strrchr(path, '/'), *bs = strrchr(path, '\\');
        base = base > bs ? base + 1 : bs ? bs + 1 : path;
        for (const char *b = strchr(base, '['); b; b = strchr(b + 1, '['))
            if (hex_run(b + 1, 16) && b[17] == ']') {
                char id[17];
                memcpy(id, b + 1, 16);
                id[16] = 0;
                title = strtoull(id, NULL, 16);
                ntik = -1;  // (from the name)
                result = title == TITLE_BASE ? GAME_NSP_NAMED : result;
                break;
            }
    }
    if (ntik) {
        if (title == TITLE_BASE) { if (ntik > 0) result = GAME_NSP_BASE; }
        else if (title == TITLE_UPDATE) {
            snprintf(why, why_size, "an update package (title id %016llX%s): the base game's package is needed", (unsigned long long)title,
                     ntik < 0 ? ", from its file name" : "");
            return GAME_NSP_UPDATE;
        } else {
            snprintf(why, why_size, title >> 12 == (TITLE_BASE >> 12) + 1
                         ? "DLC for the game (title id %016llX%s), not the game itself"
                         : "another game or add-on (title id %016llX%s), not Skyward Sword HD (01002DA013484000)",
                     (unsigned long long)title, ntik < 0 ? ", from its file name" : "");
            return GAME_NSP_OTHER;
        }
    }
    why[0] = 0;
    return result;
}

typedef struct {
    char path[GAME_FIND_PATH];
    int kind, depth;
} Cand;

typedef struct {
    GameFind *g;
    Cand c[256];
    int n, full;
    char exefs[16][GAME_FIND_PATH], romfs[16][GAME_FIND_PATH];  // every exefs / romfs folder seen, in search order
    int nexefs, nromfs;
    Cand u[16];  // the update packages seen: used only together with a base game package
    char uwhy[16][200];
    int nu;
} Walk;

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void walk(Walk *w, const char *dir, int depth) {
    GameFind *g = w->g;
    if (depth > MAX_DEPTH) return;
    if (g->dirs >= MAX_DIRS) { g->budget_hit = 1; return; }
    char p[GAME_FIND_PATH];
    snprintf(p, sizeof p, "%s/main", dir);
    char q[GAME_FIND_PATH];
    snprintf(q, sizeof q, "%s/main.npdm", dir);
    int is_game_dir = 0;  // (an exefs or romfs folder holds nothing else to look at)
    if (is_file(p) && is_file(q)) {
        if (w->nexefs < 16) snprintf(w->exefs[w->nexefs++], GAME_FIND_PATH, "%s", dir);
        is_game_dir = 1;
    }
    snprintf(p, sizeof p, "%s/region_common", dir);
    if (is_dir(p)) {
        if (w->nromfs < 16) snprintf(w->romfs[w->nromfs++], GAME_FIND_PATH, "%s", dir);
        is_game_dir = 1;
    }
    if (is_game_dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    g->dirs++;
    char **sub = NULL;
    int nsub = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *n = e->d_name;
        if (n[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, n);
        g->files++;
        if (has_ext(n, ".nsp")) {
            if (!is_file(p)) continue;
            g->nsp_seen++;
            char why[200];
            int k = game_nsp_classify(p, why, sizeof why);
            if (k == GAME_NSP_UPDATE && w->nu < 16) {
                snprintf(w->u[w->nu].path, sizeof w->u[0].path, "%s", p);
                w->u[w->nu].depth = depth;
                snprintf(w->uwhy[w->nu++], sizeof w->uwhy[0], "%s", why);
            } else if (k < 0) note(g, p, why);
            else if (w->n < 256) { snprintf(w->c[w->n].path, sizeof w->c[0].path, "%s", p); w->c[w->n].kind = k; w->c[w->n].depth = depth; w->n++; }
            else w->full = 1;
            continue;
        }
        if (has_ext(n, ".xci") || has_ext(n, ".nca") || has_ext(n, ".nsz") || has_ext(n, ".xcz")) {
            g->unusable = 1;
            note(g, p, has_ext(n, ".nsz") || has_ext(n, ".xcz") ? "a compressed package (.nsz/.xcz): not supported, use the .nsp"
                       : has_ext(n, ".xci") ? "a cartridge image (.xci): not supported, use the .nsp or the extracted files"
                                           : "a single .nca: not the whole package");
        }
        if (strcmp(n, "region_common") && is_dir(p)) {
            if (nsub == cap) { cap = cap ? cap * 2 : 16; sub = (char **)realloc(sub, cap * sizeof *sub); }
            sub[nsub++] = strdup(p);
        }
    }
    closedir(d);
    if (nsub) qsort(sub, nsub, sizeof *sub, cmp_str);  // (the same order on every file system)
    for (int i = 0; i < nsub; i++) {
        walk(w, sub[i], depth + 1);
        free(sub[i]);
    }
    free(sub);
}

static int better(const Cand *a, const Cand *b) {  // a is chosen over b
    if (a->kind != b->kind) return a->kind < b->kind;  // BASE (1) before UNKNOWN (2)
    if (a->depth != b->depth) return a->depth < b->depth;
    return strcmp(a->path, b->path) < 0;
}

static size_t common_prefix(const char *a, const char *b) {
    size_t i = 0, last = 0;
    for (; a[i] && a[i] == b[i]; i++)
        if (a[i] == '/') last = i;
    return a[i] == b[i] ? i : last;  // (up to the last whole folder name)
}

// The game's dump: the exefs folder the check accepts, with the romfs folder closest to it in the tree.
static void pick_dump(Walk *w, GameFindExefsCheck check, GameFind *g) {
    for (int i = 0; i < w->nexefs; i++) {
        char why[200] = "";
        if (check && !check(w->exefs[i], why, sizeof why)) {
            note(g, w->exefs[i], why[0] ? why : "not a supported version of the game");
            continue;
        }
        int best = -1;
        size_t bp = 0;
        for (int j = 0; j < w->nromfs; j++) {
            size_t pre = common_prefix(w->exefs[i], w->romfs[j]);
            if (best < 0 || pre > bp) best = j, bp = pre;
        }
        if (best < 0) {
            if (!g->exefs[0]) snprintf(g->exefs, sizeof g->exefs, "%s", w->exefs[i]);  // (exefs without romfs: reported)
            continue;
        }
        snprintf(g->exefs, sizeof g->exefs, "%s", w->exefs[i]);
        snprintf(g->romfs, sizeof g->romfs, "%s", w->romfs[best]);
        return;
    }
}

static void find_in(const char *dir, GameFindExefsCheck check, GameFind *g) {
    memset(g, 0, sizeof *g);
    Walk *w = (Walk *)calloc(1, sizeof *w);
    if (!w) return;
    w->g = g;
    if (is_file(dir) && has_ext(dir, ".nsp")) {  // (the package itself was picked)
        char why[200];
        g->nsp_seen = 1;
        int k = game_nsp_classify(dir, why, sizeof why);
        if (k == GAME_NSP_UPDATE) {
            snprintf(w->u[0].path, sizeof w->u[0].path, "%s", dir);
            snprintf(w->uwhy[w->nu++], sizeof w->uwhy[0], "%s", why);
        } else if (k < 0) note(g, dir, why);
        else { snprintf(w->c[0].path, sizeof w->c[0].path, "%s", dir); w->c[0].kind = k; w->n = 1; }
    } else {
        walk(w, dir, 0);
    }
    pick_dump(w, check, g);
    if (g->exefs[0] && g->romfs[0]) {  // (the extracted files are used as they are: any packages next to them are left alone)
        free(w);
        return;
    }
    int best = -1;
    for (int i = 0; i < w->n; i++)
        if (best < 0 || better(&w->c[i], &w->c[best])) best = i;
    for (int i = 0; i < w->n; i++) {
        if (i == best) continue;
        char why[GAME_FIND_PATH + 80];
        snprintf(why, sizeof why, "also a game package, but %s is used instead", w->c[best].path);
        note(g, w->c[i].path, why);
    }
    int ubest = -1;  // (the update to apply: shallowest, then name, like the base)
    for (int i = 0; i < w->nu && best >= 0; i++)
        if (ubest < 0 || w->u[i].depth < w->u[ubest].depth ||
            (w->u[i].depth == w->u[ubest].depth && strcmp(w->u[i].path, w->u[ubest].path) < 0)) ubest = i;
    for (int i = 0; i < w->nu; i++) {
        if (i == ubest) continue;
        char why[GAME_FIND_PATH + 80];
        if (ubest >= 0) snprintf(why, sizeof why, "also an update package, but %s is used instead", w->u[ubest].path);
        note(g, w->u[i].path, ubest >= 0 ? why : w->uwhy[i]);
    }
    if (ubest >= 0) snprintf(g->update, sizeof g->update, "%s", w->u[ubest].path);
    if (best >= 0) {
        snprintf(g->nsp, sizeof g->nsp, "%s", w->c[best].path);
        int others = w->n - 1;
        snprintf(g->nsp_why, sizeof g->nsp_why, "%s%s",
                 w->c[best].kind == GAME_NSP_BASE ? "the base game's package (title id 01002DA013484000)"
                 : w->c[best].kind == GAME_NSP_NAMED ? "a valid package whose file name has the base game's title id"
                                                     : "a valid package that doesn't say which game it is, so that is only confirmed when it is opened",
                 others > 0 ? "; the first of several by folder depth, then name" : "");
    }
    free(w);
}

// The folder above dir ("" if there is none worth looking at: a drive root or the file system root is never searched).
static int parent_dir(const char *dir, char *out, size_t n) {
    snprintf(out, n, "%s", dir);
    size_t len = strlen(out);
    while (len > 1 && (out[len - 1] == '/' || out[len - 1] == '\\')) out[--len] = 0;
    while (len > 0 && out[len - 1] != '/' && out[len - 1] != '\\') len--;
    while (len > 1 && (out[len - 1] == '/' || out[len - 1] == '\\')) len--;
    out[len] = 0;
    return len > 0 && out[len - 1] != ':';
}

void game_find(const char *dir, GameFindExefsCheck check, GameFind *g) {
    find_in(dir, check, g);
    if (!g->exefs[0] || g->romfs[0] || g->nsp[0]) return;
    // Only an exefs folder, nothing to go with it: the player probably picked the exefs's own folder (or a sibling of the
    // romfs), so the romfs may sit next to it. Look one level up, bounded like the first search; an unrelated parent changes nothing.
    char up[GAME_FIND_PATH];
    if (!parent_dir(dir, up, sizeof up)) return;
    GameFind *u = (GameFind *)calloc(1, sizeof *u);
    if (!u) return;
    find_in(up, check, u);
    // shortcut: only when the parent search settles on the same exefs, so a neighbour's dump is never taken, if the parent holds several dumps pick_dump decides and this may miss, then refine pick_dump to pair by exefs.
    if (u->romfs[0] && !strcmp(u->exefs, g->exefs)) {
        *g = *u;
        g->from_parent = 1;
    }
    free(u);
}

static void put(char *out, size_t n, size_t *len, const char *fmt, const char *a, const char *b) {
    if (*len >= n) return;
    int r = snprintf(out + *len, n - *len, fmt, a, b);
    if (r > 0) *len += (size_t)r < n - *len ? (size_t)r : n - *len - 1;
}

void game_find_describe(const GameFind *g, char *out, size_t n) {
    size_t len = 0;
    if (n) out[0] = 0;
    char num[96];
    if (g->exefs[0] && g->romfs[0]) {
        put(out, n, &len, "Using the extracted game files in %s%s.\n", g->exefs,
            g->from_parent ? " (the folder you picked had no romfs, so the folder above it was searched too)" : "");
    } else if (g->exefs[0] && !g->nsp[0]) {
        put(out, n, &len, "Found an exefs folder (%s) but no romfs folder (the one with region_common) to go with it.\n", g->exefs, "");
    } else if (g->nsp[0]) {
        put(out, n, &len, "Using the package %s (%s).\n", g->nsp, g->nsp_why);
        if (g->update[0]) put(out, n, &len, "With the update package %s (version 1.0.1), applied over the base game.\n", g->update, "");
    } else {
        snprintf(num, sizeof num, "%d folder%s and %d file%s", g->dirs, g->dirs == 1 ? "" : "s", g->files, g->files == 1 ? "" : "s");
        put(out, n, &len, "Looked through %s: ", num, "");
        put(out, n, &len, "no exefs + romfs folders and no usable .nsp.%s\n",
            g->budget_hit ? " (The search stopped early because the folder is very large: choose a more specific one.)" : "", "");
    }
    for (int i = 0; i < g->nnotes; i++) {
        const char *base = strrchr(g->notes[i].path, '/');
        put(out, n, &len, "- skipped %s: ", base ? base + 1 : g->notes[i].path, "");
        put(out, n, &len, "%s\n", g->notes[i].why, "");
    }
    if (g->nmore) {
        snprintf(num, sizeof num, "%d", g->nmore);
        put(out, n, &len, "- and %s more\n", num, "");
    }
}
