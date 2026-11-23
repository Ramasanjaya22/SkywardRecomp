// Game-file detection (runtime/game_find.c): which folder/.nsp the start screen picks, and what it tells the user.
// Fixtures are built in the scratch folder given as argv[1]. GAME_FIND_LEGACY=1 runs the same checks against the first
// version's logic (the first .nsp in directory order, unchecked) to show the regression tests fail on it.
#include "game_find.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char g_root[900];
static int g_legacy;
static GameFindExefsCheck g_check;

static void mkdirs(const char *path) {
    char p[1000];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; MKDIR(p); *s = '/'; }
    MKDIR(p);
}

static void write_file(const char *path, const void *data, size_t n) {
    char dir[1000];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; mkdirs(dir); }
    FILE *f = fopen(path, "wb");
    if (!f) { printf("can't write %s\n", path); exit(2); }
    fwrite(data, 1, n, f);
    fclose(f);
}

static void put32(unsigned char *p, unsigned v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }

// A PFS0 container holding the named files, each 8 bytes of data. cut > 0: the file is cut to that many bytes (a stopped copy).
static void pfs0(const char *path, const char *const *names, int n, long cut) {
    unsigned char buf[8192] = {0};
    unsigned strsz = 0;
    char strtab[2048];
    for (int i = 0; i < n; i++) { strcpy(strtab + strsz, names[i]); strsz += (unsigned)strlen(names[i]) + 1; }
    memcpy(buf, "PFS0", 4);
    put32(buf + 4, (unsigned)n);
    put32(buf + 8, strsz);
    unsigned so = 0;
    for (int i = 0; i < n; i++) {
        unsigned char *e = buf + 16 + 24 * i;
        put32(e, (unsigned)(8 * i));
        put32(e + 8, 8);
        put32(e + 16, so);
        so += (unsigned)strlen(names[i]) + 1;
    }
    size_t head = 16 + 24 * (size_t)n;
    memcpy(buf + head, strtab, strsz);
    head += strsz;
    for (int i = 0; i < 8 * n; i++) buf[head + i] = (unsigned char)(0xA0 + i);
    size_t total = head + 8 * (size_t)n;
    write_file(path, buf, cut > 0 && (size_t)cut < total ? (size_t)cut : total);
}

static const char *const BASE[] = {"01002da013484000000000000000000a.tik", "aaaa.nca", "bbbb.cnmt.nca", "01002da013484000000000000000000a.cert"};
static const char *const UPDATE[] = {"01002da013484800000000000000000a.tik", "cccc.nca", "dddd.cnmt.nca"};
static const char *const DLC[] = {"01002da013485000000000000000000a.tik", "eeee.nca"};
static const char *const OTHER[] = {"0100000000010000000000000000000b.tik", "ffff.nca"};
static const char *const NOTIK[] = {"aaaa.nca", "bbbb.cnmt.nca"};
static const char *const NOCA[] = {"01002da013484000000000000000000a.tik", "x.cert"};

static void path_of(char *out, size_t n, const char *scenario, const char *rel) {
    snprintf(out, n, "%s/%s%s%s", g_root, scenario, rel[0] ? "/" : "", rel);
}

static void dump_files(const char *scenario, const char *under) {
    char p[1000], rel[600];
    snprintf(rel, sizeof rel, "%s%sexefs/main", under, under[0] ? "/" : "");
    path_of(p, sizeof p, scenario, rel);
    write_file(p, "x", 1);
    snprintf(rel, sizeof rel, "%s%sexefs/main.npdm", under, under[0] ? "/" : "");
    path_of(p, sizeof p, scenario, rel);
    write_file(p, "x", 1);
    snprintf(rel, sizeof rel, "%s%sromfs/region_common/a.bin", under, under[0] ? "/" : "");
    path_of(p, sizeof p, scenario, rel);
    write_file(p, "x", 1);
}

static void junk(const char *scenario) {  // files that have nothing to do with the game
    char p[1000];
    const char *names[] = {"notes.txt", "cover.jpg", "setup.exe", "Lockpick_RCM.bin", "readme.nfo", "sub/readme.md", "sub/deeper/data.bin"};
    for (int i = 0; i < 7; i++) { path_of(p, sizeof p, scenario, names[i]); write_file(p, "junk", 4); }
}

// ---- the first version's logic (launcher.mm before this change), for GAME_FIND_LEGACY=1 -------------------------------------------
static int ends_ext(const char *n, const char *e) {
    size_t a = strlen(n), b = strlen(e);
    if (a <= b) return 0;
    for (size_t i = 0; i < b; i++) if (tolower((unsigned char)n[a - b + i]) != e[i]) return 0;
    return 1;
}
static int isdir_(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int isfile_(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static void legacy_walk(const char *dir, int depth, GameFind *g) {
    if (depth > 4 || (g->exefs[0] && g->romfs[0]) || g->nsp[0]) return;
    char p[1000], q[1000];
    snprintf(p, sizeof p, "%s/main", dir), snprintf(q, sizeof q, "%s/main.npdm", dir);
    if (!g->exefs[0] && isfile_(p) && isfile_(q)) snprintf(g->exefs, sizeof g->exefs, "%s", dir);
    snprintf(p, sizeof p, "%s/region_common", dir);
    if (!g->romfs[0] && isdir_(p)) snprintf(g->romfs, sizeof g->romfs, "%s", dir);
    DIR *d = opendir(dir);
    if (!d) return;
    char *sub[256]; int ns = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (ends_ext(e->d_name, ".nsp")) { if (!g->nsp[0]) snprintf(g->nsp, sizeof g->nsp, "%s", p); continue; }
        if (ends_ext(e->d_name, ".xci") || ends_ext(e->d_name, ".nca") || ends_ext(e->d_name, ".nsz") || ends_ext(e->d_name, ".xcz")) g->unusable = 1;
        if (strcmp(e->d_name, "region_common") && isdir_(p) && ns < 256) sub[ns++] = strdup(p);
    }
    closedir(d);
    for (int i = 0; i < ns; i++) { legacy_walk(sub[i], depth + 1, g); free(sub[i]); }
}
static void find(const char *dir, GameFind *g) {
    if (!g_legacy) { game_find(dir, g_check, g); return; }
    memset(g, 0, sizeof *g);
    legacy_walk(dir, 0, g);
}

static int reject_badgame(const char *exefs, char *why, size_t n) {  // stands in for the launcher's build id check
    if (!strstr(exefs, "badgame")) return 1;
    snprintf(why, n, "an unsupported build of the game");
    return 0;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: test_game_find <scratch dir>\n"); return 2; }
    snprintf(g_root, sizeof g_root, "%s", argv[1]);
    for (char *s = g_root; *s; s++) if (*s == '\\') *s = '/';
    g_legacy = getenv("GAME_FIND_LEGACY") != NULL;
    char p[1000], q[1000], text[4000];
    GameFind g;

    // A: one valid base NSP next to prod.keys and unrelated files
    path_of(p, sizeof p, "A", "Zelda [01002DA013484000][v0].nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "A", "prod.keys"); write_file(p, "header_key = 00", 15);
    junk("A");
    path_of(p, sizeof p, "A", ""); find(p, &g);
    CHECK(g.nsp[0] && ends_with(g.nsp, "Zelda [01002DA013484000][v0].nsp"), "A: picked '%s'", g.nsp);
    CHECK(!g.exefs[0] && !g.romfs[0], "A: no dump expected");

    // B: an extracted dump (exefs + romfs) with unrelated files, no NSP
    dump_files("B", "Game");
    junk("B");
    path_of(p, sizeof p, "B", ""); find(p, &g);
    CHECK(g.exefs[0] && g.romfs[0] && !g.nsp[0], "B: exefs '%s' romfs '%s' nsp '%s'", g.exefs, g.romfs, g.nsp);
    CHECK(ends_with(g.exefs, "/B/Game/exefs") && ends_with(g.romfs, "/B/Game/romfs"), "B: the folders that hold them: '%s' '%s'", g.exefs, g.romfs);

    // C: several packages: update, DLC, another game, damaged one, and the base (named so the wrong ones sort and list first)
    path_of(p, sizeof p, "C", "a_update.nsp"); pfs0(p, UPDATE, 3, 0);
    path_of(p, sizeof p, "C", "b_dlc.nsp"); pfs0(p, DLC, 2, 0);
    path_of(p, sizeof p, "C", "c_other game.nsp"); pfs0(p, OTHER, 2, 0);
    path_of(p, sizeof p, "C", "d_broken.nsp"); write_file(p, "this is not a switch package at all", 36);
    path_of(p, sizeof p, "C", "e_cut.nsp"); pfs0(p, BASE, 4, 200);
    path_of(p, sizeof p, "C", "z_skyward base.nsp"); pfs0(p, BASE, 4, 0);
    junk("C");
    path_of(p, sizeof p, "C", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/C/z_skyward base.nsp"), "C: picked '%s' (update/DLC/other game/damaged must be skipped)", g.nsp);
    if (!g_legacy) {
        CHECK(g.nnotes == 4, "C: 4 skipped candidates explained (the update is used, not skipped), got %d", g.nnotes);
        CHECK(ends_with(g.update, "/C/a_update.nsp"), "C: the update package is picked with the base: '%s'", g.update);
        game_find_describe(&g, text, sizeof text);
        CHECK(strstr(text, "a_update.nsp") && strstr(text, "update package") && strstr(text, "applied over"), "C: says the update is applied:\n%s", text);
        CHECK(strstr(text, "d_broken.nsp") && strstr(text, "PFS0"), "C: says the broken file isn't a package:\n%s", text);
        CHECK(strstr(text, "e_cut.nsp") && strstr(text, "cut short"), "C: says the truncated file is cut short:\n%s", text);
    }

    // D: several equal base packages: always the same one (shallowest, then by name), whatever the directory order
    path_of(p, sizeof p, "D", "zz/deep.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "D", "b.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "D", "a.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "D", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/D/a.nsp"), "D: picked '%s'", g.nsp);
    if (!g_legacy) CHECK(g.nnotes == 2, "D: the two others are listed, got %d", g.nnotes);

    // E: the package is in a folder named for the game (like a download manager makes), below the picked one, among other games
    path_of(p, sizeof p, "E", "Other Game/other.nsp"); pfs0(p, OTHER, 2, 0);
    path_of(p, sizeof p, "E", "Skyward Sword HD (NSP) (Base Game)/Skyward [01002DA013484000][v0].nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "E", "Another/another.nsp"); pfs0(p, OTHER, 2, 0);
    path_of(p, sizeof p, "E", ""); find(p, &g);
    CHECK(strstr(g.nsp, "Skyward [01002DA013484000][v0].nsp") != NULL, "E: picked '%s'", g.nsp);

    // F: only invalid packages: nothing is picked and the reasons say why
    path_of(p, sizeof p, "F", "update.nsp"); pfs0(p, UPDATE, 3, 0);
    path_of(p, sizeof p, "F", "garbage.nsp"); write_file(p, "", 0);
    path_of(p, sizeof p, "F", "nocontent.nsp"); pfs0(p, NOCA, 2, 0);
    path_of(p, sizeof p, "F", ""); find(p, &g);
    CHECK(!g.nsp[0] && !g.update[0], "F: nothing valid, picked '%s' + update '%s' (an update alone is no game)", g.nsp, g.update);
    if (!g_legacy) {
        game_find_describe(&g, text, sizeof text);
        CHECK(strstr(text, "update.nsp") && strstr(text, "garbage.nsp") && strstr(text, "nocontent.nsp"), "F: every file explained:\n%s", text);
        CHECK(strstr(text, "no game data"), "F: package without data:\n%s", text);
    }

    // G: empty folder, and a folder of unrelated files only
    path_of(p, sizeof p, "G_empty", ""); mkdirs(p); find(p, &g);
    CHECK(!g.nsp[0] && !g.exefs[0] && !g.romfs[0] && !g.unusable, "G: empty folder finds nothing");
    junk("G_wrong");
    path_of(p, sizeof p, "G_wrong", ""); find(p, &g);
    CHECK(!g.nsp[0] && !g.exefs[0] && !g.romfs[0], "G: unrelated files find nothing");
    if (!g_legacy) {
        game_find_describe(&g, text, sizeof text);
        CHECK(strstr(text, "no exefs + romfs folders and no usable .nsp") && strstr(text, "folders and"), "G: says what was searched:\n%s", text);
    }

    // H: a package without a ticket (its game can't be told without keys) is the fallback, a ticketed base beats it
    path_of(p, sizeof p, "H", "noticket.nsp"); pfs0(p, NOTIK, 2, 0);
    path_of(p, sizeof p, "H", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/H/noticket.nsp"), "H: ticketless package is used when nothing better: '%s'", g.nsp);
    path_of(p, sizeof p, "H", "zbase.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "H", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/H/zbase.nsp"), "H: the ticketed base wins over the ticketless one: '%s'", g.nsp);

    // I: upper-case extension, and the package itself passed as the "folder"
    path_of(p, sizeof p, "I", "GAME.NSP"); pfs0(p, BASE, 4, 0);
    path_of(q, sizeof q, "I", ""); find(q, &g);
    CHECK(ends_with(g.nsp, "/I/GAME.NSP"), "I: .NSP in capitals: '%s'", g.nsp);
    if (!g_legacy) { game_find(p, NULL, &g); CHECK(ends_with(g.nsp, "/I/GAME.NSP"), "I: package given directly: '%s'", g.nsp); }

    // J: a dump wins over a package next to it
    dump_files("J", "");
    path_of(p, sizeof p, "J", "extra.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "J", ""); find(p, &g);
    CHECK(g.exefs[0] && g.romfs[0], "J: the extracted files are found");

    // K: unsupported formats only: flagged, nothing picked
    path_of(p, sizeof p, "K", "game.xci"); write_file(p, "xci", 3);
    path_of(p, sizeof p, "K", ""); find(p, &g);
    CHECK(g.unusable && !g.nsp[0], "K: .xci is unusable");

    // L: a package in a folder deeper than the search goes (5 levels) is not found, and a huge tree stops (no crash)
    path_of(p, sizeof p, "L", "1/2/3/4/5/6/deep.nsp"); pfs0(p, BASE, 4, 0);
    path_of(p, sizeof p, "L", ""); find(p, &g);
    CHECK(!g.nsp[0], "L: too deep is not searched: '%s'", g.nsp);

    // M: another game's (or an unsupported version's) dump sits next to the right package: the package is used, the dump is explained
    //    (this is how the real feedback folder looks: dumps of other games next to the .nsp)
    dump_files("M", "badgame");
    path_of(p, sizeof p, "M", "Skyward/skyward.nsp"); pfs0(p, BASE, 4, 0);
    g_check = reject_badgame;
    path_of(p, sizeof p, "M", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/M/Skyward/skyward.nsp") && !g.exefs[0], "M: nsp '%s' exefs '%s'", g.nsp, g.exefs);
    if (!g_legacy) {
        game_find_describe(&g, text, sizeof text);
        CHECK(strstr(text, "unsupported build"), "M: says why the dump was skipped:\n%s", text);
    }
    // ... and a good dump beside the bad one is used, paired with the romfs next to it
    dump_files("M", "good");
    path_of(p, sizeof p, "M", ""); find(p, &g);
    CHECK(strstr(g.exefs, "/M/good/exefs") && strstr(g.romfs, "/M/good/romfs"), "M: good dump: '%s' '%s'", g.exefs, g.romfs);
    g_check = NULL;

    // N: no ticket: the file name's title id decides what it can (another game's is skipped, the base game's beats an unnamed one)
    path_of(p, sizeof p, "N", "a_unnamed.nsp"); pfs0(p, NOTIK, 2, 0);
    path_of(p, sizeof p, "N", "b_other [0100F43008C44800][v0].nsp"); pfs0(p, NOTIK, 2, 0);
    path_of(p, sizeof p, "N", "c_base [01002DA013484000][v0].nsp"); pfs0(p, NOTIK, 2, 0);
    path_of(p, sizeof p, "N", ""); find(p, &g);
    CHECK(ends_with(g.nsp, "/N/c_base [01002DA013484000][v0].nsp"), "N: picked '%s'", g.nsp);

    // O: exefs without romfs: nothing usable, and it says so
    path_of(p, sizeof p, "O", "exefs/main"); write_file(p, "x", 1);
    path_of(p, sizeof p, "O", "exefs/main.npdm"); write_file(p, "x", 1);
    path_of(p, sizeof p, "O", ""); find(p, &g);
    CHECK(g.exefs[0] && !g.romfs[0] && !g.nsp[0], "O: exefs only");
    if (!g_legacy) {
        game_find_describe(&g, text, sizeof text);
        CHECK(strstr(text, "no romfs folder"), "O: says romfs is missing:\n%s", text);
    }

    printf("%d checks, %d failed\n", g_checks, g_fail);
    if (!g_fail) printf("all checks passed\n");
    return g_fail != 0;
}
