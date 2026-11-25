#include "save_emu.h"
#include "platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_EMU_FILE "wiiking2.sav"
/* Only Skyward Sword HD 1.0.0 is supported; its title id is fixed here. */
#define SAVE_EMU_TITLE "01002DA013484000"
#define SAVE_EMU_MAX_HITS 64
#define SAVE_EMU_WALK_DEPTH 12

typedef struct {
    const char *name;
    const char *display;
    const char *process; /* folder under the host's data dir */
    const char *ryujinx; /* title id; only Ryujinx's layout needs one */
} EmuDef;

static const EmuDef EMUS[] = {
    {"eden", "Eden", "eden", NULL},
    {"yuzu", "Yuzu", "yuzu", NULL},
    {"suyu", "Suyu", "suyu", NULL},
    {"sudachi", "Sudachi", "sudachi", NULL},
    {"citron", "Citron", "citron", NULL},
    {"torzu", "Torzu", "torzu", NULL},
    {"fritchii", "Fritchii", "fritchii", NULL},
    {"lichii", "Lichii", "lichii", NULL},
    {"eglantine", "Eglantine", "eglantine", NULL},
    {"ryujinx", "Ryujinx", "ryujinx", SAVE_EMU_TITLE},
};

typedef struct {
    const EmuDef *def;  /* NULL for the generic "folder" search */
    SaveEmuHit *out;
    int max;
    int count;
} HitCtx;

static void join(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s/%s", a, b);
}

static void slashify(char *s)
{
    for (; *s; ++s)
        if (*s == '\\') *s = '/';
}

static long long file_when(const char *path)
{
    struct stat st;
    return stat(path, &st) ? 0 : (long long)st.st_mtime;
}

static void add_hit(HitCtx *ctx, const char *file, const char *profile)
{
    int i;
    const char *slash = strrchr(file, '/'), *bs = strrchr(file, '\\');
    const char *base = bs > slash ? bs : slash;
    char folder[1100];
    if (!base) return;
    snprintf(folder, sizeof folder, "%.*s", (int)(base - file), file);
    for (i = 0; i < ctx->count; ++i)
        if (!strcmp(ctx->out[i].path, folder)) return;
    if (ctx->count >= ctx->max) return;
    SaveEmuHit *h = &ctx->out[ctx->count++];
    memset(h, 0, sizeof *h);
    snprintf(h->path, sizeof h->path, "%s", folder);
    if (ctx->def) {
        snprintf(h->emu, sizeof h->emu, "%s", ctx->def->name);
        snprintf(h->display, sizeof h->display, "%s", ctx->def->display);
    } else {
        snprintf(h->emu, sizeof h->emu, "folder");
    }
    snprintf(h->profile, sizeof h->profile, "%s", profile);
    h->when = file_when(file);
}

static const char *profile_of(const EmuDef *def, const char *path, char *profile, size_t cap)
{
    char rel[1200];
    const char *p, *q;
    char want[128];

    snprintf(rel, sizeof rel, "%s", path);
    slashify(rel);

    if (!strcmp(def->name, "ryujinx")) {
        /* <ryu>/bis/user/save/<id>/0/wiiking2.sav */
        p = strstr(rel, "bis/user/save/");
        if (!p) return NULL;
        p += strlen("bis/user/save/");
        q = strchr(p, '/');
        if (!q || strcmp(q, "/0/wiiking2.sav")) return NULL;
        snprintf(profile, cap, "%.*s", (int)(q - p), p);
        return profile;
    }

    /* Eden: <id>/<profile>/<title>/wiiking2.sav
       Fritchii: <id>/<profile>/<title>/0/wiiking2.sav */
    p = strstr(rel, "nand/user/save/");
    if (!p) return NULL;
    p += strlen("nand/user/save/");
    q = strchr(p, '/');
    if (!q) return NULL;
    q++; /* the account id is a wildcard */
    {
        const char *r = strchr(q, '/');
        if (!r) return NULL;
        snprintf(profile, cap, "%.*s", (int)(r - q), q);
        q = r + 1;
    }
    snprintf(want, sizeof want, "%s/wiiking2.sav", SAVE_EMU_TITLE);
    if (!strcmp(q, want)) return profile;
    snprintf(want, sizeof want, "%s/0/wiiking2.sav", SAVE_EMU_TITLE);
    if (!strcmp(q, want)) return profile;
    return NULL;
}

static void walk(const char *dir, HitCtx *ctx, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char p[1200];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        join(p, sizeof p, dir, e->d_name);
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < SAVE_EMU_WALK_DEPTH) walk(p, ctx, depth + 1);
            } else if (!strcmp(e->d_name, SAVE_EMU_FILE)) {
                const char *slash = strrchr(p, '/'), *bs = strrchr(p, '\\');
                const char *base = bs > slash ? bs : slash;
                char folder[1100];
                if (!base) continue;
                snprintf(folder, sizeof folder, "%.*s", (int)(base - p), p);
                if (ctx->def) {
                    char profile[64];
                    if (profile_of(ctx->def, p, profile, sizeof profile))
                        add_hit(ctx, p, profile);
                } else {
                    const char *sep = strrchr(folder, '/'), *bsep = strrchr(folder, '\\');
                    if (bsep > sep) sep = bsep;
                    add_hit(ctx, p, sep ? sep + 1 : folder);
                }
        }
    }
    closedir(d);
}

static int compare_hits(const void *a, const void *b)
{
    const SaveEmuHit *x = a, *y = b;
    if (x->when > y->when) return -1;
    if (x->when < y->when) return 1;
    return strcmp(x->path, y->path);
}

/* The host's data folder; APPDATA is only authoritative on Windows. */
static void data_root(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    out[0] = 0;
    if (!home || !*home) home = getenv("USERPROFILE");
#ifdef _WIN32
    {
        const char *appdata = getenv("APPDATA");
        if (appdata && *appdata) {
            snprintf(out, cap, "%s", appdata);
            return;
        }
        if (home && *home) snprintf(out, cap, "%s/AppData/Roaming", home);
    }
#elif defined(__APPLE__)
    if (home && *home) snprintf(out, cap, "%s/Library/Application Support", home);
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        if (xdg && *xdg) snprintf(out, cap, "%s", xdg);
        else if (home && *home) snprintf(out, cap, "%s/.local/share", home);
    }
#endif
}

/* Ryujinx keeps its saves in its config folder, not the data folder. */
static void ryujinx_root(char *out, size_t cap, const char *data)
{
    const char *home = getenv("HOME");
    if (!home || !*home) home = getenv("USERPROFILE");
#if defined(_WIN32) || defined(__APPLE__)
    join(out, cap, data, "Ryujinx");
#else
    {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) join(out, cap, xdg, "Ryujinx");
        else if (home && *home) {
            char cfg[1100];
            join(cfg, sizeof cfg, home, ".config");
            join(out, cap, cfg, "Ryujinx");
        } else join(out, cap, data, "Ryujinx");
    }
#endif
}

int save_emu_find(SaveEmuHit *out, int max)
{
    char data[1100], root[1100];
    HitCtx ctx;
    int i;

    if (!out || max <= 0) return 0;
    memset(&ctx, 0, sizeof ctx);
    ctx.out = out;
    ctx.max = max;
    data_root(data, sizeof data);
    if (!data[0]) return 0;
    for (i = 0; i < (int)(sizeof EMUS / sizeof EMUS[0]); ++i) {
        const EmuDef *def = &EMUS[i];
        if (!strcmp(def->name, "ryujinx")) ryujinx_root(root, sizeof root, data);
        else join(root, sizeof root, data, def->process);
        ctx.def = def;
        walk(root, &ctx, 0);
    }
    qsort(out, ctx.count, sizeof *out, compare_hits);
    return ctx.count;
}

int save_emu_find_under(const char *dir, const char *emu, SaveEmuHit *out, int max)
{
    HitCtx ctx;
    const EmuDef *def = NULL;
    int i;

    if (!dir || !out || max <= 0) return 0;
    for (i = 0; i < (int)(sizeof EMUS / sizeof EMUS[0]); ++i)
        if (!strcmp(emu, EMUS[i].name)) def = &EMUS[i];
    if (!def && strcmp(emu, "folder")) return 0;
    memset(&ctx, 0, sizeof ctx);
    ctx.def = def;
    ctx.out = out;
    ctx.max = max;
    walk(dir, &ctx, 0);
    qsort(out, ctx.count, sizeof *out, compare_hits);
    return ctx.count;
}

bool save_emu_running(char *display, size_t n)
{
    if (!display || !n)
        return false;
    display[0] = '\0';
    const size_t count = sizeof EMUS / sizeof EMUS[0];
    const char *names[sizeof EMUS / sizeof EMUS[0] + 1];
    for (size_t i = 0; i < count; ++i)
        names[i] = EMUS[i].process;
    names[count] = NULL;
    char found[32];
    if (!plat_process_running(names, found, sizeof found))
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(found, EMUS[i].process)) {
            snprintf(display, n, "%s", EMUS[i].display);
            return true;
        }
    }
    return false;
}
