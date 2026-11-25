#include "save_emu.h"
#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "paths.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

static bool export_fail(char *why, size_t cap, const char *fmt, ...)
{
    if (why && cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(why, cap, fmt, ap);
        va_end(ap);
    }
    return false;
}

static bool export_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static bool export_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool export_is_link(const char *p)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(p, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

static void export_mkdirs(const char *path)
{
    char p[1100];
    size_t i;

    snprintf(p, sizeof p, "%s", path);
    for (i = 1; p[i]; ++i) {
        if (p[i] == '/' || p[i] == '\\') {
            char s = p[i];
            p[i] = 0;
            mkdir(p, 0755);
            p[i] = s;
        }
    }
    mkdir(p, 0755);
}

static bool export_copy_file(const char *src, const char *dst, char *why, size_t cap)
{
    char buf[1 << 16];
    size_t k;
    FILE *in = fopen(src, "rb");
    FILE *out;

    if (!in) return export_fail(why, cap, "Couldn't read the save (%s).", strerror(errno));
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return export_fail(why, cap, "Couldn't write the save (%s).", strerror(errno));
    }
    while ((k = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, k, out) != k) {
            fclose(in);
            fclose(out);
            return export_fail(why, cap, "Couldn't write the save (%s).", strerror(errno));
        }
    }
    bool bad = ferror(in);
    if (fclose(in) != 0) bad = true;
    if (fclose(out) != 0) bad = true;
    if (bad) return export_fail(why, cap, "Couldn't copy the save (%s).", strerror(errno));
    return true;
}

static bool export_copy_saves(const char *from, const char *to, char *why, size_t cap)
{
    static const char *names[] = {
        SAVE_EMU_FILE, "skip.dat", "0.screenshot", "1.screenshot",
        "2.screenshot", "3.screenshot"
    };
    char src[1100], dst[1100];
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        struct stat st;
        join(src, sizeof src, from, names[i]);
        if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (export_is_link(src))
            return export_fail(why, cap, "%s is a link, not a save file.", names[i]);
        if (st.st_size > (4ULL << 20))
            return export_fail(why, cap, "%s is too big to be a save file.", names[i]);
        join(dst, sizeof dst, to, names[i]);
        export_mkdirs(to);
        if (!export_copy_file(src, dst, why, cap)) return false;
    }
    return true;
}

static void export_safe_name(char *out, size_t n, const char *emu)
{
    size_t i = 0;

    for (; emu && *emu && i + 1 < n; ++emu) {
        char c = *emu;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out[i++] = c;
        else out[i++] = '_';
    }
    out[i] = 0;
    if (!i) snprintf(out, n, "folder");
}

bool save_emu_export(const char *root, const char *dst, const char *emu,
                     char *backup_out, size_t bn, char *why, size_t cap)
{
    char live[1100], save[1100], partial[1100], final[1100], name[128], safe[64];
    char sub[1100], sibling[1100], old[1100], fresh[1100];
    const char *base, *bs;
    bool has_sibling = false;
    time_t t;

    if (why && cap) why[0] = 0;
    if (backup_out && bn) backup_out[0] = 0;
    if (!root || !dst || !emu)
        return export_fail(why, cap, "No save of the game there: start it once in the emulator and save.");

    join(live, sizeof live, root, "save/save_0");
    join(save, sizeof save, live, SAVE_EMU_FILE);
    if (!export_exists(save))
        return export_fail(why, cap, "No save of the game there: start it once in the emulator and save.");
    join(save, sizeof save, dst, SAVE_EMU_FILE);
    if (!export_exists(save))
        return export_fail(why, cap, "No save of the game there: start it once in the emulator and save.");

    export_safe_name(safe, sizeof safe, emu);
    t = time(NULL);
    for (;; ++t) {
        const struct tm *lt = localtime(&t);
        if (lt) {
            snprintf(name, sizeof name, "export-%s-%04d%02d%02d-%02d%02d%02d", safe,
                     lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                     lt->tm_hour, lt->tm_min, lt->tm_sec);
        } else {
            snprintf(name, sizeof name, "export-%s-00000000-000000", safe);
        }
        snprintf(partial, sizeof partial, "%s/backups/%s.partial", root, name);
        snprintf(final, sizeof final, "%s/backups/%s", root, name);
        if (!export_exists(partial) && !export_exists(final)) break;
    }

    bs = strrchr(dst, '/');
    base = strrchr(dst, '\\');
    if (bs > base) base = bs;
    if (base && !strcmp(base + 1, "0")) {
        snprintf(sibling, sizeof sibling, "%.*s/1", (int)(base - dst), dst);
        has_sibling = export_exists(sibling) && export_is_dir(sibling);
    }

    export_mkdirs(partial);
    join(sub, sizeof sub, partial, base && base[1] ? base + 1 : "save");
    export_mkdirs(sub);
    if (!export_copy_saves(dst, sub, why, cap)) {
        plat_remove_tree(partial);
        return false;
    }
    if (has_sibling) {
        join(sub, sizeof sub, partial, "1");
        export_mkdirs(sub);
        if (!export_copy_saves(sibling, sub, why, cap)) {
            plat_remove_tree(partial);
            return false;
        }
    }
    if (rename(partial, final) != 0) {
        export_fail(why, cap, "Couldn't finish the backup (%s).", strerror(errno));
        plat_remove_tree(partial);
        return false;
    }

    snprintf(fresh, sizeof fresh, "%s.skyrecomp-new", dst);
    snprintf(old, sizeof old, "%s.skyrecomp-old", dst);
    plat_remove_tree(fresh);
    export_mkdirs(fresh);
    // Keep the emulator's other files: only the save files are overwritten.
    if (export_is_dir(dst) && !rt_copy_tree(dst, fresh, why, cap)) {
        plat_remove_tree(fresh);
        return false;
    }
    if (!export_copy_saves(live, fresh, why, cap)) {
        plat_remove_tree(fresh);
        return false;
    }

    plat_remove_tree(old);
    if (rename(dst, old) != 0) {
        export_fail(why, cap, "Couldn't replace the emulator's save: a file is in use (is the emulator running?) (%s).", strerror(errno));
        plat_remove_tree(fresh);
        return false;
    }
    if (rename(fresh, dst) != 0) {
        export_fail(why, cap, "Couldn't put the save in place (%s).", strerror(errno));
        if (rename(old, dst) != 0)
            export_fail(why, cap, "Couldn't put the old save back (%s).", strerror(errno));
        plat_remove_tree(fresh);
        return false;
    }
    plat_remove_tree(old);
    // Ryujinx rebuilds 1/ from 0/ when it mounts the save.
    if (has_sibling) plat_remove_tree(sibling);

    if (backup_out && bn) snprintf(backup_out, bn, "%s", final);
    return true;
}
