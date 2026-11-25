#include "save_backup.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "paths.h"
#include "platform.h"
#include "save_emu.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_UNITS 64
typedef char Unit[400];  // a folder of saves, relative to a root: "save" or "rando/<seed>/save"

static void jn(char *o, size_t n, const char *a, const char *b) { snprintf(o, n, "%s/%s", a, b); }
static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static bool exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void mkdirs(const char *path) {
    char p[1100];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++)
        if (*c == '/' || *c == '\\') { char s = *c; *c = 0; mkdir(p, 0755); *c = s; }
    mkdir(p, 0755);
}

static void parent_dirs(const char *file) {
    char p[1100], *c;
    snprintf(p, sizeof p, "%s", file);
    if ((c = strrchr(p, '/'))) *c = 0, mkdirs(p);
}

static unsigned long long tree_bytes(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return (unsigned long long)st.st_size;
    unsigned long long sum = 0;
    DIR *d = opendir(p);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        char c[1100];
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) jn(c, sizeof c, p, e->d_name), sum += tree_bytes(c);
    }
    if (d) closedir(d);
    return sum;
}

// The save folders of a root (or of a backup): save/, and rando/<seed>/save/ for each seed.
static int find_units(const char *base, Unit *out) {
    int n = 0;
    char p[1100];
    jn(p, sizeof p, base, "save");
    if (is_dir(p)) snprintf(out[n++], sizeof(Unit), "save");
    jn(p, sizeof p, base, "rando");
    DIR *d = opendir(p);
    struct dirent *e;
    while (d && n < MAX_UNITS && (e = readdir(d))) {
        char u[400], q[1100];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(u, sizeof u, "rando/%s/save", e->d_name);
        jn(q, sizeof q, base, u);
        if (is_dir(q)) snprintf(out[n++], sizeof(Unit), "%s", u);
    }
    if (d) closedir(d);
    return n;
}

static bool fail(char *why, size_t cap, const char *msg, const char *arg) {
    if (cap) snprintf(why, cap, msg, arg);
    return false;
}

static bool copy_unit(const char *from, const char *to, const char *unit, char *why, size_t cap) {
    char s[1100], d[1100];
    jn(s, sizeof s, from, unit);
    jn(d, sizeof d, to, unit);
    parent_dirs(d);
    return rt_copy_tree(s, d, why, cap);
}

static bool valid_name(const char *s) {
    int y, mo, d, h, mi, se, k = 0;
    if (sscanf(s, "save-%4d%2d%2d-%2d%2d%2d%n", &y, &mo, &d, &h, &mi, &se, &k) != 6) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) return false;
    return (!s[k] || !strcmp(s + k, "-auto")) && strlen(s) < 64;
}

static int by_name_desc_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)b, *(const char *const *)a);
}

// Every finished backup name, newest first. Scans the whole folder (a fixed cap here used to hide backups past it),
// and removes the ".partial" shells a killed backup leaves behind so they can't accumulate forever.
static char **backup_names(const char *root, int *count) {
    *count = 0;
    char dir[1100];
    jn(dir, sizeof dir, root, "backups");
    DIR *d = opendir(dir);
    if (!d) return NULL;
    char **v = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        char p[1100];
        jn(p, sizeof p, dir, e->d_name);
        if (!valid_name(e->d_name)) {
            if (!strncmp(e->d_name, "save-", 5) && strstr(e->d_name, ".partial") && is_dir(p)) plat_remove_tree(p);
            continue;
        }
        if (!is_dir(p)) continue;
        if (n == cap) {
            int nc = cap ? cap * 2 : 32;
            char **nv = realloc(v, (size_t)nc * sizeof *nv);
            if (!nv) break;
            v = nv, cap = nc;
        }
        v[n] = strdup(e->d_name);
        if (!v[n]) break;
        n++;
    }
    closedir(d);
    qsort(v, (size_t)n, sizeof *v, by_name_desc_str);
    *count = n;
    return v;
}

int save_backup_list(const char *root, SaveBackup *out, int max) {
    int n = 0;
    char **names = backup_names(root, &n);
    if (!names || max <= 0) { free(names); return 0; }
    int k = 0;
    for (; k < n && k < max; k++) {
        SaveBackup *b = &out[k];
        struct tm t = {0};
        snprintf(b->name, sizeof b->name, "%s", names[k]);
        sscanf(names[k], "save-%4d%2d%2d-%2d%2d%2d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
        t.tm_year -= 1900, t.tm_mon -= 1, t.tm_isdst = -1;
        b->when = (long long)mktime(&t);
        char p[1100];
        snprintf(p, sizeof p, "%s/backups/%s", root, names[k]);
        b->bytes = tree_bytes(p);
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    return k;
}

// The oldest past the newest SAVE_BACKUP_KEEP removed; the backups a restore/import reads from are never touched.
// ponytail: name order decides "oldest" (a clock set back can misorder); `keep` protects the ones that matter.
static void prune(const char *root, const char *keep, const char *keep2) {
    int n = 0;
    char **names = backup_names(root, &n);
    if (!names) return;
    int left = n;
    for (int i = n - 1; i >= 0 && left > SAVE_BACKUP_KEEP; i--) {
        if ((keep && !strcmp(names[i], keep)) || (keep2 && !strcmp(names[i], keep2))) continue;
        char p[1100];
        snprintf(p, sizeof p, "%s/backups/%s", root, names[i]);
        if (plat_remove_tree(p)) left--;
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

// A cheap content fingerprint: file names and bytes, no mtimes (a copy changes those). Equal fingerprints mean an
// automatic backup would be a duplicate, so the old one is kept instead of filling the folder with identical copies.
static uint64_t fp_str(uint64_t h, const char *s) {
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    return h;
}

static uint64_t tree_fp(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    uint64_t h = 1469598103934665603ULL;
    if (!S_ISDIR(st.st_mode)) {
        FILE *f = fopen(p, "rb");
        if (!f) return 0;
        unsigned char buf[8192];
        size_t r;
        uint64_t total = 0;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0 && total < (4ULL << 20)) {
            for (size_t i = 0; i < r; i++) h = (h ^ buf[i]) * 1099511628211ULL;
            total += r;
        }
        fclose(f);
        return h ^ total;
    }
    DIR *d = opendir(p);
    struct dirent *e;
    char **names = NULL;
    int n = 0;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char **nv = realloc(names, (size_t)(n + 1) * sizeof *nv);
        if (!nv) break;
        names = nv;
        names[n++] = strdup(e->d_name);
    }
    if (d) closedir(d);
    qsort(names, (size_t)n, sizeof *names, by_name_desc_str);
    for (int i = 0; i < n; i++) {
        char c[1100];
        h = fp_str(h, names[i]);
        jn(c, sizeof c, p, names[i]);
        h ^= tree_fp(c) + 1099511628211ULL;
        free(names[i]);
    }
    free(names);
    return h;
}

static uint64_t units_fp(const char *base, Unit *units, int count) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < count; i++) {
        char p[1100];
        h = fp_str(h, units[i]);
        jn(p, sizeof p, base, units[i]);
        h ^= tree_fp(p) + 1099511628211ULL;
    }
    return h;
}

// autom: the backup before a restore/import: nothing to back up is fine then (name_out empty)
static bool create(const char *root, bool autom, char *name_out, size_t n, char *why, size_t cap) {
    Unit units[MAX_UNITS];
    char dir[1100], part[1100], fin[1100], name[64];
    if (n) name_out[0] = 0;
    int count = find_units(root, units);
    if (!count) return autom || fail(why, cap, "There's no save data to back up.%s", "");
    if (autom) {  // an unchanged save is not another backup: compare all kept ones. Names can run ahead of the clock
        int nb = 0;  // when several are made in one second, so "newest by name" isn't necessarily newest by content.
        char **names = backup_names(root, &nb);
        uint64_t live = units_fp(root, units, count);
        for (int i = 0; i < nb; i++) {
            char b[1100];
            Unit old_units[MAX_UNITS];
            snprintf(b, sizeof b, "%s/backups/%s", root, names[i]);
            int old_count = find_units(b, old_units);
            if (old_count == count && live == units_fp(b, old_units, old_count)) {
                for (int j = 0; j < nb; j++) free(names[j]);
                free(names);
                return true;  // name_out stays empty: nothing was made
            }
        }
        for (int i = 0; i < nb; i++) free(names[i]);
        free(names);
    }
    jn(dir, sizeof dir, root, "backups");
    mkdirs(dir);
    time_t t = time(NULL);
    for (;; t++) {  // (a second taken: the next one)
        struct tm *lt = localtime(&t);
        snprintf(name, sizeof name, "save-%04d%02d%02d-%02d%02d%02d%s", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec, autom ? "-auto" : "");
        jn(fin, sizeof fin, dir, name);
        snprintf(part, sizeof part, "%s.partial", fin);
        if (!exists(fin) && !exists(part)) break;
    }
    mkdirs(part);
    for (int i = 0; i < count; i++)
        if (!copy_unit(root, part, units[i], why, cap)) { plat_remove_tree(part); return false; }
    if (rename(part, fin) != 0) {
        plat_remove_tree(part);
        return fail(why, cap, "Couldn't finish the backup (%s).", strerror(errno));
    }
    if (n) snprintf(name_out, n, "%s", name);
    // (the new one kept: a clock set back, or backups made faster than one a second, can leave it the oldest name)
    if (!autom) prune(root, name, NULL);  // (an auto one is pruned after the swap: its source may be the oldest)
    return true;
}

bool save_backup_create(const char *root, char *name_out, size_t n, char *why, size_t cap) {
    if (cap) why[0] = 0;
    return create(root, false, name_out, n, why, cap);
}

bool save_backup_auto(const char *root, char *name_out, size_t n) {
    char name[64], why[300];
    if (n) name_out[0] = 0;
    bool made = create(root, true, name, sizeof name, why, sizeof why) && name[0];
    if (made) prune(root, name, NULL);  // (the new one kept, as in create)
    if (n) snprintf(name_out, n, "%s", made ? name : "");
    return made;
}

bool save_backup_on_update(const char *root, const char *version, char *name_out, size_t n) {
    char path[1100], have[128] = "";
    if (n) name_out[0] = 0;
    jn(path, sizeof path, root, "backups/last_version.txt");
    FILE *f = fopen(path, "rb");
    if (f) { have[fread(have, 1, sizeof have - 1, f)] = 0; fclose(f); }
    if (!strcmp(have, version)) return false;
    // (no file: a first run, or an update from a build that didn't write it - a backup either way, when there are saves)
    bool made = save_backup_auto(root, name_out, n);
    if (made) {
        // Only note the version once the backup exists: a failed one is retried next boot instead of being lost.
        parent_dirs(path);
        if ((f = fopen(path, "wb"))) { fputs(version, f); fclose(f); }
    }
    return made;
}

// The save folders of `from` staged beside the ones in root, then swapped in: a folder is either the old or the new
// one, and when one can't be swapped (a file held open) the ones swapped before it are put back.
static bool swap_in(const char *root, const char *from, Unit *units, int count, char *why, size_t cap) {
    char s[1100], o[1100], tmp[1100], src[1100];
    bool ok = true;
    int i, done = 0;
    for (i = 0; i < count && ok; i++) {
        jn(s, sizeof s, root, units[i]);
        snprintf(tmp, sizeof tmp, "%s.restoring", s);
        plat_remove_tree(tmp);
        jn(src, sizeof src, from, units[i]);
        ok = rt_copy_tree(src, tmp, why, cap);
    }
    for (i = 0; i < count && ok; i++, done++) {
        jn(s, sizeof s, root, units[i]);
        snprintf(tmp, sizeof tmp, "%s.restoring", s);
        snprintf(o, sizeof o, "%s.replaced", s);
        plat_remove_tree(o);
        if (exists(s) && rename(s, o) != 0) {
            ok = fail(why, cap, "Couldn't replace the saves: a file is in use (is the game running?) (%s).", strerror(errno));
            break;
        }
        if (rename(tmp, s) != 0) {
            ok = fail(why, cap, "Couldn't put the saves in place (%s).", strerror(errno));
            if (exists(o)) rename(o, s);
            break;
        }
    }
    for (i = 0; ok && i < count; i++) {
        jn(s, sizeof s, root, units[i]);
        snprintf(o, sizeof o, "%s.replaced", s);
        plat_remove_tree(o);
    }
    if (ok) return true;
    for (i = 0; i < done; i++) {  // (put back what was swapped)
        jn(s, sizeof s, root, units[i]);
        snprintf(tmp, sizeof tmp, "%s.restoring", s);
        snprintf(o, sizeof o, "%s.replaced", s);
        rename(s, tmp);
        if (exists(o)) rename(o, s);
    }
    for (i = 0; i < count; i++) {
        jn(s, sizeof s, root, units[i]);
        snprintf(tmp, sizeof tmp, "%s.restoring", s);
        plat_remove_tree(tmp);
    }
    return false;
}

bool save_backup_restore(const char *root, const char *name, char *why, size_t cap) {
    Unit units[MAX_UNITS];
    char src[1100];
    if (cap) why[0] = 0;
    if (!valid_name(name)) return fail(why, cap, "That isn't a backup (%s).", name);
    snprintf(src, sizeof src, "%s/backups/%s", root, name);
    int count = is_dir(src) ? find_units(src, units) : 0;
    if (!count) return fail(why, cap, "The backup %s has no saves in it.", name);
    char made[64] = "";
    if (!create(root, true, made, sizeof made, why, cap)) return false;  // (what is there now, kept)
    bool ok = swap_in(root, src, units, count, why, cap);
    if (ok) prune(root, name, made[0] ? made : NULL);  // keep the source and the just-made safety copy
    else if (made[0]) {  // a failed swap must not leave an extra backup of unchanged saves behind
        char p[1100];
        snprintf(p, sizeof p, "%s/backups/%s", root, made);
        plat_remove_tree(p);
    }
    return ok;
}

static bool is_save_file(const char *n) {
    size_t l = strlen(n);
    return !strcmp(n, "zelda.sav") || !strcmp(n, "wiiking2.sav") ||
           (!strncmp(n, "savedata", 8) && l > 12 && !strcmp(n + l - 4, ".bin"));
}

static bool is_side_file(const char *n) {
    size_t l = strlen(n);
    return !strcmp(n, "skip.dat") ||
           (l >= 12 && n[0] >= '0' && n[0] <= '9' && !strcmp(n + l - 11, ".screenshot")) ||
           (l > 7 && !strcmp(n + l - 7, ".flagsx"));
}

static bool is_link(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

// The save files in dir put in stage/save/<slot>/. A save a different size from the one it replaces (or empty) is
// refused. shortcut: the game's own save size isn't a constant anywhere in the code, so the save in place is the
// reference; with none in place any non-empty file is taken.
// Returns the saves found, -1 when one is refused or can't be copied.
static int stage_dir(const char *root, const char *dir, const char *slot, const char *stage, char *why, size_t cap) {
    int found = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        char f[1100], live[1100], dst[1100], name[256];
        size_t l = strlen(e->d_name);
        bool side = is_side_file(e->d_name);
        if (!is_save_file(e->d_name) && !side) continue;
        jn(f, sizeof f, dir, e->d_name);
        if (is_link(f)) {
            closedir(d);
            if (cap) snprintf(why, cap, "%s is a link, not a save file.", e->d_name);
            return -1;
        }
        if (tree_bytes(f) > (4ULL << 20)) {
            closedir(d);
            if (cap) snprintf(why, cap, "%s is too big to be a save file.", e->d_name);
            return -1;
        }
        snprintf(name, sizeof name, "%s", !strcmp(e->d_name, "zelda.sav") ? "savedata0.bin" : e->d_name);
        snprintf(live, sizeof live, "%s/save/%s/%s", root, slot, name);
        unsigned long long sz = tree_bytes(f), want = tree_bytes(live);
        if (!side) {
            if (!sz || (want && sz != want)) {
                closedir(d);
                if (cap) snprintf(why, cap, "%s isn't a save of this game (%llu bytes, %llu expected).", e->d_name, sz, want);
                return -1;
            }
            found++;
        }
        snprintf(dst, sizeof dst, "%s/save/%s/%s", stage, slot, name);
        parent_dirs(dst);
        if (!rt_copy_tree(f, dst, why, cap)) return closedir(d), -1;
    }
    if (d) closedir(d);
    return found;
}

bool save_backup_import(const char *root, const char *src_dir, char *why, size_t cap) {
    Unit units[MAX_UNITS];
    char stage[1100], live[1100];
    if (cap) why[0] = 0;
    if (!is_dir(src_dir)) return fail(why, cap, "%s isn't a folder.", src_dir);
    int count = find_units(src_dir, units);
    const char *from = src_dir;
    if (count && !tree_bytes(src_dir)) count = 0;
    if (!count) {  // not a backup: save files, put on top of the saves in place
        snprintf(stage, sizeof stage, "%s/backups/.import.partial", root);
        plat_remove_tree(stage);
        mkdirs(stage);
        jn(live, sizeof live, root, "save");
        if (is_dir(live) && !copy_unit(root, stage, "save", why, cap)) return plat_remove_tree(stage), false;
        int n = stage_dir(root, src_dir, "save_0", stage, why, cap);  // (the folder itself: slot save_0)
        DIR *d = n >= 0 ? opendir(src_dir) : NULL;
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char sub[1100];
            jn(sub, sizeof sub, src_dir, e->d_name);
            if (strncmp(e->d_name, "save_", 5) || !is_dir(sub)) continue;
            int k = stage_dir(root, sub, e->d_name, stage, why, cap);
            n = k < 0 ? -1 : n + k;
            if (n < 0) break;
        }
        if (d) closedir(d);
        if (n <= 0) {
            plat_remove_tree(stage);
            if (n < 0) return plat_remove_tree(stage), false;
            SaveEmuHit hits[8];
            int found = save_emu_find_under(src_dir, "folder", hits, 8);
            if (found > 1) {
                if (cap) snprintf(why, cap, "That folder has saves of %d profiles: pick the profile's folder.", found);
                return plat_remove_tree(stage), false;
            }
            if (found != 1)
                return plat_remove_tree(stage), fail(why, cap, "No save file in that folder.%s", "");

            /* the folder fallback stages only save_0; copy the live save first so
               its other slots survive the whole-save swap */
            mkdirs(stage);
            if (is_dir(live) && !copy_unit(root, stage, "save", why, cap))
                return plat_remove_tree(stage), false;
            if (stage_dir(root, hits[0].path, "save_0", stage, why, cap) <= 0)
                return plat_remove_tree(stage), fail(why, cap, "No save file in that folder.%s", "");
        }
        snprintf(units[0], sizeof(Unit), "save");
        count = 1, from = stage;
    }
    char made[64] = "";
    bool ok = create(root, true, made, sizeof made, why, cap) && swap_in(root, from, units, count, why, cap);
    if (from == stage) plat_remove_tree(stage);
    if (ok) prune(root, made[0] ? made : NULL, NULL);  // keep the fresh safety copy; never prune on a failed import
    else if (made[0]) {
        char p[1100];
        snprintf(p, sizeof p, "%s/backups/%s", root, made);
        plat_remove_tree(p);
    }
    return ok;
}
