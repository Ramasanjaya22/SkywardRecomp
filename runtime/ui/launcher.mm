// Start screen, shown before the game boots (user 2026-10-08, styled after Dusklight's launcher): PLAY (with
// ‹ Vanilla / seed › picker), SETTINGS, QUIT, game-files status at the lower left. Nothing of the game ships with
// SkywardRecomp: on first launch the user picks the folder with their own game files (1.0.0, or 1.0.1 with its update package), we check
// the main module's build id (runtime/game_addresses/versions.h), copy the files and build the game's data image; PLAY stays disabled until that worked. The background is a frame of the game's own title movie (from the user's files) once extracted.
//
// Skipped with --play, LARECOMP_NO_LAUNCHER=1, or any scripted test run (LARECOMP_INPUT): tests boot straight in
// (with LARECOMP_NO_LAUNCHER, set by tools/game_slot.sh, the app exits rather than show it: runtime/main.c).
// The in-game menu's "Title screen" comes back here without leaving the process (runtime/main.c reboots the game).
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <atomic>

#include "imgui.h"
#include "imgui_internal.h"  // (SetKeyOwner)
#include "option_rows.h"
#include "palette.h"
#include "extract_current.h"
#include "osk.h"
#include "pad_nav.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
#endif
#include "imgui_impl_sdl3.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "platform.h"
#include "paths.h"
#include "save_backup.h"
#include "gamebuild.h"
#include "build_cache.h"
#include "game_addresses.h"
#include "game_find.h"
#include "gamelib.h"
#include "romfs_build.h"
#include "settings.h"
#include "input_bind.h"
#include "nvn/shader_prep.h"
#include "assets/backdrop_data.h"

#include <cstdio>
bool controls_ui_took_back(void);  // ui/controls_ui.mm: B went back a step on Configure inputs
void ui_window_init(void);
SDL_Window *ui_window(void);
#ifdef __APPLE__
void *ui_metal_layer(void);
void *ui_metal_device(void);
#else
// runtime/nvn/vulkan: window frames and ImGui plumbing
bool vk_frame_begin(const float clear[4]);
void vk_antilag_pause(bool on);
void vk_frame_end(const char *shot_path);
void *vk_frame_cmd(void);
bool vk_imgui_backend_init(void);
void vk_imgui_backend_shutdown(void);
uint64_t vk_imgui_texture_rgba(const void *rgba, int w, int h);
#endif
}
#ifdef __APPLE__
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// (test) BGRA texture -> PNG (the game renderer's saver isn't up yet before the game boots)
static void save_png(id<MTLTexture> tex, const char *path) {
    size_t w = tex.width, h = tex.height;
    std::vector<uint8_t> px(w * h * 4);
    [tex getBytes:px.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (dest) {  // (NULL when the file can't be created, e.g. its folder isn't there: CFRelease(NULL) traps)
        CGImageDestinationAddImage(dest, img, NULL);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    } else fprintf(stderr, "[launcher] can't write %s\n", path);
    CFRelease(url), CGImageRelease(img), CGContextRelease(ctx), CGColorSpaceRelease(cs);
}
#endif

// The release tag this build is (also names the release files; Makefile / tools/bundle_macos.sh / make_dmg.sh read
// it). Under the hood every release is a semantic version (0.<n>.<m> during the beta: .0 releases bring features, the
// rest are fixes). Tags: beta_<n> and beta_<n>.<m> ("Beta <n>.<m>", = 0.<n>.<m>) up to Beta 5; from Beta 6
// v0.<n>.<m> ("Beta <n>", "Beta <n>.<m>"); after the beta v<major>.<minor>.<patch> ("1.0.0"). A
// test build (a pre-release for a few players to try a fix) is test-<name>: the start screen says "Test build <name>",
// and it previews LARECOMP_TEST_OF - the update check offers that release (or a later one) as soon as it's out, so its
// players move to it.
#define LARECOMP_VERSION "v0.8.2"
#define LARECOMP_TEST_OF "v0.8.0"
extern "C" const char *larecomp_version(void) { return LARECOMP_VERSION; }

// ------------------------------------------------------------------ update check
// The start screen asks GitHub for the project's releases once per launch (a background curl, which macOS and
// Windows 10+ ship) and offers the best one if it outranks this build (runtime/ui/update_tags.h): any v1.0.0+ above
// every beta, betas by number, a beta's fixes releases (beta_<n>.<m>, v0.<n>.<m>) between it and the next beta, and
// v0.<n>.<m> the same as beta_<n>.<m>; other tags (the alpha_* ones) are ignored. (Beta 5 and earlier only know
// beta_<n>: they never offer a hotfix; builds before Beta 5.3 don't know v0.<n>.<m> either.) Offline / no curl /
// anything unexpected: nothing is shown.
#define RELEASES_URL "https://api.github.com/repos/Ramasanjaya22/SkywardRecomp/releases?per_page=100"
#define RELEASE_PAGE "https://github.com/Ramasanjaya22/SkywardRecomp/releases/tag/"

#include "update_tags.h"  // tag_rank, tag_label

static std::string g_update_tag;  // a newer release's tag (g_update_lock)
static int g_update_state;        // 0 not asked yet, 1 asking, 2 asked (g_update_tag: the newer release, if any), 3 failed (g_update_lock)
static pthread_mutex_t g_update_lock = PTHREAD_MUTEX_INITIALIZER;

static void *update_thread(void *) {
    std::string body;
    if (const char *t = getenv("LARECOMP_UPDATE_TAGS")) {  // (test) "beta_2,v1.0.0": these releases, no network
        for (const char *p = t; *p;) {
            const char *c = strchr(p, ',');
            body += "\"tag_name\": \"" + std::string(p, c ? c - p : strlen(p)) + "\"\n";
            p = c ? c + 1 : p + strlen(p);
        }
    } else {
#ifdef _WIN32
        const char *sr = getenv("SystemRoot");
        std::string curl = std::string(sr ? sr : "C:\\Windows") + "\\System32\\curl.exe";
#else
        std::string curl = "/usr/bin/curl";
#endif
        std::string out = std::string(rt_data_root()) + "/cache/releases.json";
        mkdir((std::string(rt_data_root()) + "/cache").c_str(), 0755);
        const char *av[] = {curl.c_str(), "-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json",
                            "-A", "SkywardRecomp/" LARECOMP_VERSION, RELEASES_URL, NULL};
        PlatProc p = plat_spawn(av, out.c_str(), false);
        if (!p || plat_proc_wait(p, true) != 0) {
            remove(out.c_str());
            pthread_mutex_lock(&g_update_lock);
            g_update_state = 3;
            pthread_mutex_unlock(&g_update_lock);
            return NULL;
        }
        if (FILE *f = fopen(out.c_str(), "rb")) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && body.size() < (8u << 20)) body.append(buf, n);
            fclose(f);
        }
        remove(out.c_str());
    }
    std::string best;
    // (a test build ranks just below the release it previews; other test builds rank -1: never offered)
    long long best_rank = strncmp(LARECOMP_VERSION, "test-", 5) ? tag_rank(LARECOMP_VERSION) : tag_rank(LARECOMP_TEST_OF) - 1;
    for (size_t at = 0; (at = body.find("\"tag_name\"", at)) != std::string::npos;) {
        size_t q1 = body.find('"', body.find(':', at) + 1), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        if (tag_rank(tag) > best_rank) best_rank = tag_rank(tag), best = tag;
        at = q2;
    }
    if (!best.empty()) fprintf(stderr, "[launcher] update available: %s (this build %s)\n", best.c_str(), LARECOMP_VERSION);
    pthread_mutex_lock(&g_update_lock);
    g_update_tag = best;
    g_update_state = 2;
    pthread_mutex_unlock(&g_update_lock);
    return NULL;
}

// The start screen's UPDATE row asks again (the window can stay open while a release comes out); one check at a time.
static void start_update_check() {
    pthread_mutex_lock(&g_update_lock);
    bool go = g_update_state != 1;
    if (go) g_update_state = 1;
    pthread_mutex_unlock(&g_update_lock);
    pthread_t t;
    if (go && pthread_create(&t, NULL, update_thread, NULL) == 0) pthread_detach(t);
    else if (go) {
        pthread_mutex_lock(&g_update_lock);
        g_update_state = 3;
        pthread_mutex_unlock(&g_update_lock);
    }
}

// ------------------------------------------------------------------ game files
static std::string g_root;  // extracted game: <root>/exefs, <root>/romfs

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// The game's RomFS has exactly game_romfs_files() files (per version, 0: unknown): a copy with fewer is incomplete (a copy or unzip that
// stopped partway) and the game crashes on the first one it misses (issues #24, #35). Only the number ships.
// Their sizes add up to game_romfs_bytes() (per version): every file there with another total is a damaged or
// changed file (issue #391). The game still plays then; the start screen says so.
// With check (base: where the path inside romfs starts), the files that aren't the game's are named in the log, the
// first ROMFS_ODD_LOGGED of them (check->odd counts them all), and copies of the game code at the romfs root
// (game_romfs_exefs_copy, game: the game folder) are left out of the count and the bytes (check->copies).
enum { ROMFS_ODD_LOGGED = 20 };
struct RomfsCheckWalk { GameRomfsCheck *check; size_t base; int odd; const char *game; int copies; };
static int count_files(const std::string &dir, int depth = 0, uint64_t *bytes = NULL, RomfsCheckWalk *check = NULL) {
    DIR *d = depth < 12 ? opendir(dir.c_str()) : NULL;
    if (!d) return 0;
    int n = 0;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;  // (".", "..", .DS_Store)
        std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) n += count_files(p, depth + 1, bytes, check);
        else if (S_ISREG(st.st_mode)) {
            if (check && depth == 0 && game_romfs_exefs_copy(check->game, e->d_name, (uint64_t)st.st_size)) {
                check->copies++;
                continue;
            }
            n++;
            if (bytes) *bytes += (uint64_t)st.st_size;
            if (check && !game_romfs_check_file(check->check, p.c_str() + check->base, (uint64_t)st.st_size) &&
                check->odd++ < ROMFS_ODD_LOGGED)
                fprintf(stderr, "[launcher] romfs: not one of the game's files: %s (%llu bytes)\n", p.c_str() + check->base,
                        (unsigned long long)st.st_size);
        }
    }
    closedir(d);
    return n;
}
// The copied romfs' file count and bytes, counted once (the checks below run every frame): -1 = count again (game
// files set up, deleted)
static int g_romfs_count = -1;
static uint64_t g_romfs_bytes, g_romfs_want;  // (want: this version's total, 0 unknown)
static int g_romfs_missing;  // the game's files not there by name and size (a renamed or moved file: same count)
static std::atomic<bool> g_build_cache_changed{true};
static std::atomic<int> g_files_changes{0};  // (the version label on the start screen is read again)
static void game_files_changed(void) { g_romfs_count = -1; g_build_cache_changed = true; ++g_files_changes; }
// The setup page redraws every frame; read the build cache's disk state only after the files/build state changes.
static bool g_resumable, g_has_build_cache;
static void read_build_cache(void) {
    if (!g_build_cache_changed.exchange(false)) return;
    g_resumable = build_cache_resumable(g_root.c_str());
    g_has_build_cache = build_cache_present(g_root.c_str());
}
static bool resumable_build(void) { read_build_cache(); return g_resumable; }
static bool has_build_cache(void) { read_build_cache(); return g_has_build_cache; }

static int romfs_want(const std::string &root) { return game_romfs_files(game_version_of(root.c_str())); }
static std::string incomplete(int have, int want) {
    char b[96];
    if (want) snprintf(b, sizeof b, "Game files incomplete (%d of %d files)", have, want);
    else snprintf(b, sizeof b, "Game files incomplete (no game data)");
    return b;
}

static const char *const WRONG_VERSION = "Wrong version of the game";

// The copied game files alone ("" = there), without the game code built from them
static std::string check_copied_files(const std::string &root) {
    if (!file_exists(root + "/exefs/main")) return "No game files yet";
    if (!game_version_of(root.c_str())) return WRONG_VERSION;
    if (!file_exists(root + "/romfs/region_common")) {
        if (file_exists(root + "/romfs.bin")) {  // the extracted image (a .nsp setup): served whole (fs.c)
            struct stat st;
            if (stat((root + "/romfs.bin").c_str(), &st) == 0) {
                g_romfs_count = romfs_want(root), g_romfs_missing = 0, g_romfs_bytes = (uint64_t)st.st_size;
                g_romfs_want = 0;  // (no per-file bytes to compare against)
                return "";
            }
        }
        return incomplete(0, romfs_want(root));
    }
    if (g_romfs_count < 0) {
        g_romfs_bytes = 0;
        const GameVersion *v = game_version_of(root.c_str());
        RomfsCheckWalk check = {game_romfs_check_begin(v), root.size() + strlen("/romfs/"), 0, root.c_str(), 0};
        g_romfs_count = count_files(root + "/romfs", 0, &g_romfs_bytes, &check);
        int missing = game_romfs_check_end(check.check);
        g_romfs_missing = missing;
        if (check.copies)
            fprintf(stderr, "[launcher] romfs: %d %s of the game code at the root, ignored\n", check.copies,
                    check.copies == 1 ? "copy" : "copies");
        if (check.odd > ROMFS_ODD_LOGGED)
            fprintf(stderr, "[launcher] romfs: %d more files that aren't the game's\n", check.odd - ROMFS_ODD_LOGGED);
        if (missing) fprintf(stderr, "[launcher] romfs: %d of the game's files missing or changed\n", missing);
        g_romfs_want = game_romfs_bytes(v);
        if (g_romfs_count >= romfs_want(root) && g_romfs_want && g_romfs_bytes != g_romfs_want)
            fprintf(stderr, "[launcher] romfs: %llu bytes in %d files, this version's are %llu: damaged or changed "
                    "game files\n", (unsigned long long)g_romfs_bytes, g_romfs_count, (unsigned long long)g_romfs_want);
    }
    if (int want = romfs_want(root); want && g_romfs_count < want) return incomplete(g_romfs_count, want);
    return "";
}

// Every file there, but not all of them the game's (name and size) or not the game's sizes: a damaged, changed,
// renamed or moved file (issue #391). Playable, so not a check_game_files() problem: the start screen shows it beside
// "ready".
static bool files_changed(const std::string &root) {
    return check_copied_files(root).empty() && (g_romfs_missing > 0 || (g_romfs_want && g_romfs_bytes != g_romfs_want));
}
static const char *const CHANGED_HEAD = "Game files damaged or changed";
static const char *const CHANGED_HINT = "Copy your game files again, then choose that folder and set them up again.";

// "" = ready to play, else what is wrong
static std::string check_game_files(const std::string &root) {
    std::string why = check_copied_files(root);
    if (why.empty() && !gamelib_present(root.c_str())) return "Game code not built yet";
    return why;
}

// What to do about a check_game_files() problem (the line under it on the start screen)
static const char *status_hint(const std::string &why) {
    if (why.rfind("Game files incomplete", 0) == 0)
        return "Some of the game's files are missing, usually from a copy or unzip that stopped partway. Copy your game "
               "files again, then choose that folder and set them up again.";
    if (why == WRONG_VERSION) return "SkywardRecomp needs version 1.0.0 of the game, or 1.0.1 (the base game with its update).";
    if (why == "Game code not built yet") return "Set up the game files to build it (a few minutes, only once).";
    return "Set up the game files to play.";
}

// ------------------------------------------------------------------ setup (background thread)
// The player brings their own game files: a folder holding exefs/ and romfs/. They're copied into the data root (APFS
// clones / hard links where the file system allows); the game reads romfs/ in place (runtime/services/fs.c).
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_src;           // the picked folder (launcher_prepare: or a base .nsp)
static std::string g_keys_pick;     // the prod.keys the player chose (g_lock); empty: search for one
static std::string g_update_pick;   // the 1.0.1 update .nsp the player chose (g_lock); empty: the one found next to the base game
static std::string g_found;         // what the picked folder holds, as game_find_describe says it (g_lock): shown under the folder row
static bool g_scan_base, g_scan_update, g_scan_keys, g_scan_nsp; // what the folder scan found, for the setup checklist (g_lock)
static int g_copied_ok = -1;        // check_copied_files(g_root).empty() cached for the draw loop (g_lock); -1: recompute
static bool g_headless;             // launcher_prepare: no window, so no shader preparation (the game does it later)
static std::string g_job;           // current setup step / result for the status line
static std::string g_job_hint;      // a failure: what to do about it (shown under it)
static std::string g_job_tech;      // a setup failure's raw reason (shown collapsed under "Technical details")
static int g_job_kind;              // FAIL_* of the current failure, 0: none (it gets the Choose Another Folder button)
static bool g_busy, g_job_failed;
static bool g_exit_after_setup;  // (test: LARECOMP_LAUNCHER_EXTRACT)
static bool g_confirm_delete;       // game-files page: "Delete the game files?" shown

// One launcher UI state, derived from the existing checks below; no build behavior here.
enum class LauncherState {
    Ready,               // game files present and unchanged
    ReadyUpdateOptional, // ready; the 1.0.1 update can still be added
    NeedsSetup,          // setup can fix the files
    Blocked,             // wrong game version; setup cannot fix here
};

struct LauncherStateModel {
    LauncherState state = LauncherState::NeedsSetup;
    std::string status;   // check_game_files(), unchanged
    std::string hint;     // status_hint(status), unchanged
    std::string detected; // game_find_describe() text for the picked folder
    bool ready = false;
    bool changed = false;
    bool has_update = false;   // v1.0.1: the update is already installed
};

// Table plan for current Game Files rows; actions stay actions.
enum class GameFilesRowKind { Required, Optional, Detected };

struct GameFilesRowPlan {
    const char *label;
    GameFilesRowKind kind;
};

[[maybe_unused]] static const GameFilesRowPlan kGameFilesRows[] = {
    { "Game Files",                   GameFilesRowKind::Detected },
    { "Game files folder",            GameFilesRowKind::Required },
    { "Update 1.0.1 (optional .nsp)", GameFilesRowKind::Optional },
    { "prod.keys (for a .nsp)",       GameFilesRowKind::Required },
};

static LauncherStateModel launcher_state_model(const std::string &status,
                                               const std::string &root) {
    LauncherStateModel m;
    m.status = status;
    m.hint = status_hint(status);
    m.detected = g_found;
    m.ready = status.empty();
    m.changed = m.ready && files_changed(root);
    if (m.ready) {
        const GameVersion *v = game_version_of(root.c_str());
        m.has_update = v && strcmp(v->key, "v1_0_1") == 0;
    }
    if (!m.ready)
        m.state = status == WRONG_VERSION ? LauncherState::Blocked
                                          : LauncherState::NeedsSetup;
    else
        m.state = m.has_update ? LauncherState::Ready
                               : LauncherState::ReadyUpdateOptional;
    return m;
}

static void set_job(const std::string &s, bool failed = false, const std::string &hint = "") {
    pthread_mutex_lock(&g_lock);
    g_job = s;
    g_job_failed = failed;
    g_job_hint = hint;
    g_job_tech.clear(), g_job_kind = 0;
    pthread_mutex_unlock(&g_lock);
}

// (the not-found failure's hint, and the caption over the game files page's example folder)
static const char *const PICK_FOLDER_HINT =
    "Choose the folder with exefs and romfs, then press Set up game files.\n"
    "A .nsp also needs prod.keys; the 1.0.1 update is optional.";

// Setup failures: a short headline and what to do next (also LARECOMP_LAUNCHER_SHOT=<f>:fail<1-6>, screenshots)
enum { FAIL_NOT_FOUND = 1, FAIL_UNUSABLE, FAIL_COPY, FAIL_VERSION, FAIL_MISSING, FAIL_BUILD };
static void set_failure(int k, int have = -1, const char *reason = NULL, int want = 0) {
    static const char *const head[] = {"", "No game files found", "Game files are packaged",
                                       "Could not copy the game files", "Wrong game version", "Game files incomplete",
                                       "Game code not built"};
    // Each: why, then the next action. The raw reason goes to g_job_tech (the "Technical details" section), not in here.
    static const std::string hint[] = {
        "",
        "Why: no game files were found in that folder.\nNext: choose the folder with exefs and romfs (a .nsp also needs prod.keys).",
        "Why: .xci, .nsp and .nca packages can't be read directly.\nNext: extract them into a folder and choose that folder.",
        "Why: the files couldn't be written to the data folder.\nNext: check its free space and permissions, then try again.",
        "Why: version 1.0.0 or 1.0.1 is required.\nNext: choose the base game together with its 1.0.1 update.",
        "Why: some required files are missing.\nNext: choose the whole extracted game folder.",
        "Why: the game code didn't compile.\nNext: see the .log files in the build folder inside the data folder, then try again."};
    if (k < 1 || k > 6) return;
    char b[64] = "";
    if (k == FAIL_MISSING && have >= 0 && want) snprintf(b, sizeof b, "%d of %d romfs files are missing", have, want);
    set_job(head[k], true, hint[k]);
    pthread_mutex_lock(&g_lock);
    g_job_kind = k;
    g_job_tech = reason ? reason : b;
    pthread_mutex_unlock(&g_lock);
}

// What the picked folder holds (runtime/game_find.c): the extracted game files (exefs + romfs folders) or a base-game .nsp, found
// in the folder or a few levels below it. An exefs folder of another game or version is skipped, not picked, and every .nsp that
// isn't the base game (an update, DLC, another game, a damaged copy) is skipped with its reason; .xci / .nca / .nsz / .xcz are
// unusable. A base .nsp is extracted in place (extract_nsp below).
static int exefs_supported(const char *exefs, char *why, size_t n) {
    char label[32];
    if (game_release_label(exefs, label, sizeof label)) return 1;
    snprintf(why, n, "not version 1.0.0 or 1.0.1 of Skyward Sword HD (a different game, or another version of it)");
    return 0;
}

// The same description is logged and put in the failure the player reads, so a wrong pick can be seen at once.
static void find_game(const std::string &dir, GameFind &f, std::string *described = NULL) {
    game_find(dir.c_str(), exefs_supported, &f);
    char text[4096];
    game_find_describe(&f, text, sizeof text);
    fprintf(stderr, "[launcher] looking in %s (%d folders, %d files)\n%s\n", dir.c_str(), f.dirs, f.files, text);
    if (described) *described = text;
}

// Contextual setup fields: a folder with exefs/romfs needs nothing else, an NSP package may need a key file
// and can take an optional 1.0.1 update. Remember what find_game() saw so drawing never re-scans a drive.
struct PickContext {
    bool nsp;            // a base-game .nsp was picked
    bool update;         // the 1.0.1 update was found next to it
    std::string nsp_dir; // where to look for the key file
    std::string version; // the picked exefs' release ("1.0.0" / "1.0.1"), if known
    std::string key_file; // prod.keys found near the NSP (scanned once, at pick time, off the render thread)
};
static std::string find_prod_keys(const std::string &nsp_dir);
static PickContext g_pick;  // (g_lock)

static void note_pick_context(const GameFind &f) {
    PickContext c;
    c.nsp = f.nsp[0] != '\0';
    c.update = f.update[0] != '\0';
    if (c.nsp) {
        std::string p = f.nsp;
        size_t slash = p.find_last_of("/\\");
        c.nsp_dir = slash == std::string::npos ? "." : p.substr(0, slash);
    }
    char label[32] = {};
    if (game_release_label(f.exefs, label, sizeof label)) c.version = label;
    if (c.nsp) c.key_file = find_prod_keys(c.nsp_dir);
    pthread_mutex_lock(&g_lock);
    g_pick = c;
    pthread_mutex_unlock(&g_lock);
}

// A supported game version (runtime/game_addresses/versions.h)? Logs which one.
static bool build_id_ok(const std::string &exefs) {
    char label[32];
    const char *v = game_release_label(exefs.c_str(), label, sizeof label);
    fprintf(stderr, "[launcher] game files: %s\n", v ? (std::string("version ") + v).c_str() : "an unsupported version");
    return v != NULL;
}

// progress (the overlay's bar): the romfs copy (bytes)
static std::atomic<unsigned long long> g_done, g_total;
// The bar: copying is the first tenth, building the game code (runtime/gamebuild.c) the rest
static unsigned long long g_phase_base;
static std::atomic<float> g_build_frac{-1};  // -1: not building
static std::atomic<bool> g_cancel_build{false};
static bool build_cancelled(void) { return g_cancel_build.load(); }
static bool g_copy_skipped;                  // (only the game code to build: the bar is all of that)
static bool g_auto_update;                   // started by itself: game files there, game code from an older version
static void setup_progress(uint64_t done, uint64_t) { g_done = g_phase_base + done; }
static void build_progress(double f, const char *step) {
    g_build_frac = (float)f;
    static const char *last;
    if (step != last) last = step, set_job(step);
}
// Shader preparation, after the game files are ready (runtime/nvn/shader_prep.h): its own bar, and Skip
static std::atomic<float> g_prep_frac{-1};  // -1: not preparing
static volatile int g_prep_skip;
static bool g_prep_skip_ask;  // Skip pressed: "are you sure?" in front of the bar (LARECOMP_LAUNCHER_SHOT=<f>:skipask)
static void prep_progress(int done, int total) { g_prep_frac = total ? (float)done / (float)total : 0.0f; }
static void prepare_shaders(bool background = false) {
    g_prep_skip = 0, g_prep_frac = 0;
    if (!background) set_job("Preparing shaders");
    shader_prep_run((g_root + "/romfs").c_str(), prep_progress, &g_prep_skip);
    g_prep_frac = -1;
}

// Every later start: the same preparation in the background as soon as the start screen is up (quick: the system's
// shader caches already hold the compiled code). Only a PLAY before it is done shows its bar (g_prep_wait).
static std::atomic<bool> g_bg_prep{false};
static bool g_prep_wait;
static void start_background_prep(void) {
    static bool started;  // (once per process: the prepared objects stay for later boots)
    if (started) return;
    started = true;
    g_bg_prep = true;
    pthread_t th;
    pthread_create(&th, NULL, [](void *) -> void * { prepare_shaders(true); g_bg_prep = false; return NULL; }, NULL);
    pthread_detach(th);
}
// Before the game files change (setup, delete): the background preparation stops reading them
static void stop_background_prep(void) {
    if (!g_bg_prep) return;
    g_prep_skip = 1;
    while (g_bg_prep) SDL_Delay(5);
}

static float extract_progress(void) {
    if (float p = g_prep_frac; p >= 0) return p > 1 ? 1 : p;
    unsigned long long t = g_total, d = g_done;
    float b = g_build_frac, f = b >= 0 ? (g_copy_skipped ? b : 0.1f + 0.9f * b) : t ? 0.1f * (float)((double)d / (double)t) : 0;
    return f < 0 ? 0 : f > 0.99f ? 0.99f : f;
}

// Builds the game code from the copied files (a few minutes); false = failed (the job line says so)
static bool build_game_code(void) {
    // The build takes the defaults this app was built for; gamebuild_run logs generator, compiler and jobs.
    GameBuildOpts o = {0};
    o.progress = build_progress;
    o.cancelled = build_cancelled;
    o.jobs = (int)std::thread::hardware_concurrency();
    char why[512] = "";
    g_build_frac = 0;
    bool ok = gamebuild_run(g_root.c_str(), &o, why, sizeof why);
    if (!ok) {
        fprintf(stderr, "[launcher] game code: %s\n", why);
        if (g_cancel_build) set_job("Compilation cancelled. Completed files are kept.");
        else if (gamebuild_cfg_stopped())
            set_job("The game code didn't build", true,
                    "The tool that builds the game code was stopped by Windows security settings (Control Flow Guard). "
                    "If you changed Control flow guard in Exploit protection, set it back to the default and try again. "
                    "If it keeps happening, report it on GitHub.");
        else set_failure(FAIL_BUILD, -1, why[0] ? why : NULL);  // the step and the compiler's first error, not "Build failed"
    }
    return ok;
}

// The extractor's log's last non-empty line (the failure reason for the job line)
static void last_log_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[1024];
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, sz > 900 ? sz - 900 : 0, SEEK_SET);
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    buf[got] = 0;
    fclose(f);
    char *best = NULL;
    for (char *s = buf, *e; (e = strchr(s, '\n')); s = e + 1) {
        *e = 0;
        if (e > s && e[-1] == '\r') e[-1] = 0;  // (Windows log)
        if (*s) best = s;
    }
    if (!best && *buf) best = buf;  // (a last line without a newline)
    if (best) snprintf(out, n, "%s", best);
}

// ------------------------------------------------------------------ prod.keys search
// A real prod.keys is a text file of "name = hex" lines that has header_key in it; this rejects empty or unrelated files of that name.
static bool looks_like_prod_keys(const std::string &p) {
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    static char buf[1 << 20];  // (a real one is ~20 KB; only the first MB is read)
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, "header_key") != NULL;
}

// Walks dir (depth levels down) for a prod.keys, giving up after *budget directory reads so a huge drive can't stall the launcher.
static bool scan_for_keys(const std::string &dir, int depth, int *budget, std::string *out) {
    if (*budget <= 0) return false;
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    --*budget;
    std::vector<std::string> subs;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir + "/" + e->d_name;
        if (strcasecmp(e->d_name, "prod.keys") == 0 && looks_like_prod_keys(p)) { *out = p; closedir(d); return true; }
        if (depth > 0) subs.push_back(p);
    }
    closedir(d);
    for (const std::string &s : subs) {
        struct stat st;
        if (stat(s.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && scan_for_keys(s, depth - 1, budget, out)) return true;
    }
    return false;
}

// Finds the user's own prod.keys on this PC (nothing is bundled): first the exact places it is normally kept (next to the
// package, the data root, Lockpick_RCM's and the Switch emulators' folders), then a bounded search of the user's folders.
// Returns "" if none is found. shortcut: the search stops after ~4000 directory reads and 4 levels, so a key buried deeper
// or on another drive is missed; the user then puts it next to the .nsp.
static std::string find_prod_keys(const std::string &nsp_dir) {
    const char *home = getenv("HOME"), *prof = getenv("USERPROFILE"), *appdata = getenv("APPDATA");
    std::vector<std::string> cand = {nsp_dir + "/prod.keys", nsp_dir + "/../prod.keys", g_root + "/prod.keys"};
    std::vector<std::string> homes;
    for (const char *h : {home, prof}) if (h && std::find(homes.begin(), homes.end(), h) == homes.end()) homes.push_back(h);
    for (const std::string &h : homes) {
        cand.push_back(h + "/.switch/prod.keys");
        for (const char *s : {"/.local/share/yuzu/keys", "/.local/share/eden/keys", "/.config/Ryujinx/system",
                              "/Library/Application Support/Ryujinx/system"})
            cand.push_back(h + s + std::string("/prod.keys"));
    }
    if (appdata) for (const char *s : {"/yuzu/keys", "/eden/keys", "/suyu/keys", "/Ryujinx/system"})
        cand.push_back(std::string(appdata) + s + "/prod.keys");
    for (const std::string &k : cand)
        if (file_exists(k) && looks_like_prod_keys(k)) return k;

    int budget = 4000;
    std::string found;
    if (scan_for_keys(nsp_dir, 4, &budget, &found)) return found;
    for (const std::string &h : homes) {
        for (const char *s : {"/Downloads", "/Desktop", "/Documents", "/Switch", "/Games"})
            if (scan_for_keys(h + s, 4, &budget, &found)) return found;
        if (scan_for_keys(h, 1, &budget, &found)) return found;  // (the home folder itself, one level: AppData etc. are huge)
    }
    return "";
}

// Which extra fields apply to this pick: an NSP package needs a key file; its 1.0.1 update is optional,
// and hidden when found with the package or already chosen. An exefs/romfs folder needs neither field.
static bool picked_nsp_package() {
    pthread_mutex_lock(&g_lock);
    bool nsp = g_pick.nsp;
    pthread_mutex_unlock(&g_lock);
    return nsp;
}

static bool update_field_applies() {
    pthread_mutex_lock(&g_lock);
    bool applies = g_pick.nsp && !g_pick.update;  // (stays visible after a choice, to show and change it)
    pthread_mutex_unlock(&g_lock);
    return applies;
}

// The chosen key wins; otherwise what picked() found near the package. Drawing only reads.
static std::string detected_key_file() {
    pthread_mutex_lock(&g_lock);
    std::string chosen = g_keys_pick, found = g_pick.key_file;
    bool nsp = g_pick.nsp;
    pthread_mutex_unlock(&g_lock);
    if (!nsp) return "";
    if (!chosen.empty() && looks_like_prod_keys(chosen)) return chosen;
    return found;
}

// What the picked files already are: the exefs build id, or the NSP plus its optional update.
static std::string detected_game_version() {
    pthread_mutex_lock(&g_lock);
    PickContext c = g_pick;
    bool update_chosen = !g_update_pick.empty();
    pthread_mutex_unlock(&g_lock);
    if (!c.version.empty()) return c.version;
    if (c.nsp && (c.update || update_chosen)) return "1.0.1";  // (an NSP alone: version not detected)
    return "";
}

// A picked .nsp (the game package): extracted in place by the extractor (tools/extract.py, shipped as the
// skywardextract helper, or the source checkout's python) straight into extracted/ -- exefs/ + romfs.bin, the
// layout runtime/services/fs.c serves. The 1.0.1 update package, if there, is applied over the base (extract.py merges
// its BKTR patch with the base's RomFS, no external tool). Needs
// the console's prod.keys: next to the package, in its parent folder, or already in the data root.
static bool extract_nsp(const std::string &nsp, const std::string &update) {
    size_t slash = nsp.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string(".") : nsp.substr(0, slash);
    std::string keys;
    pthread_mutex_lock(&g_lock);
    keys = g_keys_pick;
    pthread_mutex_unlock(&g_lock);
    if (!keys.empty() && !looks_like_prod_keys(keys)) {  // (chosen, then moved or emptied)
        fprintf(stderr, "[launcher] chosen prod.keys is gone or invalid: %s\n", keys.c_str());
        keys.clear();
    }
    if (keys.empty()) keys = find_prod_keys(dir);
    if (keys.empty()) {
        fprintf(stderr, "[launcher] no prod.keys found (looked next to %s, in the data folder, in yuzu/Eden/Ryujinx/Lockpick folders and under the home folder)\n", nsp.c_str());
        set_job("The .nsp can't be opened without prod.keys", true,
                "A .nsp is encrypted with your Switch's own keys, kept in a file named prod.keys, and SkywardRecomp found none "
                "on this PC. Dump it from your own console with Lockpick_RCM, then choose it in the \"prod.keys (for a .nsp)\" row of this window, "
                "or put it in the same folder as the .nsp. Not needed if you have the extracted exefs and romfs folders.");
        return false;
    }
    char helper[1100], py[1100], script[1100];
    const char *argv[12];
    int n = 0;
    if (rt_resource("skywardextract/skywardextract" PLAT_EXE_SUFFIX, NULL, helper, sizeof helper)) {
        argv[n++] = helper;
    } else if (rt_resource(NULL, "tools/" PLAT_VENV_PYTHON, py, sizeof py) &&
               rt_resource(NULL, "tools/extract.py", script, sizeof script)) {
        argv[n++] = py, argv[n++] = script;
    } else if (rt_resource(NULL, "tools/extract.py", script, sizeof script)) {
        argv[n++] = "python", argv[n++] = script;  // (a checkout without the venv: the system's python)
    } else {
        set_job("The extractor is missing", true, "tools/extract.py wasn't found next to the app.");
        return false;
    }
    fprintf(stderr, "[launcher] extractor: %s%s\n", argv[0], n > 1 ? " (with tools/extract.py)" : "");
    static std::string a_base, a_keys, a_work;  // (the arguments outlive the spawn)
    static std::string a_update;
    a_update = update;
    a_base = nsp, a_keys = keys, a_work = ".";  // (cwd is the data root; extract.py appends 'extracted' itself, so passing g_root nested it as extracted/extracted)
    argv[n++] = "--base", argv[n++] = a_base.c_str();
    if (!a_update.empty()) argv[n++] = "--update", argv[n++] = a_update.c_str();
    argv[n++] = "--keys", argv[n++] = a_keys.c_str();
    argv[n++] = "--work", argv[n++] = a_work.c_str();
    argv[n++] = "--flat", argv[n] = NULL;
    rt_remove_game_files(g_root.c_str());  // (an earlier or interrupted setup's)
    game_files_changed();
    mkdir(g_root.c_str(), 0755);
    std::string log = "extract.log";  // (data-root level, outside extracted/ so extract.py's rmtree of a stale base_out can't hit the open log file)
    set_job("Extracting the game from " + nsp.substr(slash == std::string::npos ? 0 : slash + 1) +
            " (this can take several minutes)");
    g_total = 0, g_done = 0;
    fprintf(stderr, "[launcher] extracting %s (keys: %s)\n", nsp.c_str(), keys.c_str());
    PlatProc p = plat_spawn(argv, log.c_str(), false);
    if (!p) { set_job("The extractor didn't start", true, "See extract.log next to the game files."); return false; }
    int r = plat_proc_wait(p, true);
    if (r != 0 || !file_exists(g_root + "/exefs/main") || !file_exists(g_root + "/romfs.bin")) {
        char why[256];
        last_log_line(log.c_str(), why, sizeof why);
        fprintf(stderr, "[launcher] extractor exit %d, last line: %s (see %s/extract.log)\n", r, why, g_root.c_str());
        if (strstr(why, "No module named")) {  // (a source checkout run with a python that lacks the extractor's packages)
            set_job("The extractor's Python packages are missing", true,
                    std::string(why) + ". This copy of SkywardRecomp runs tools/extract.py with the system's Python, which needs "
                    "the \"cryptography\" package. Install it with: python -m pip install cryptography. "
                    "(The released installer / AppImage bundles its own extractor and doesn't need Python.)");
            return false;
        }
        set_job("The game package didn't extract", true,
                std::string(why[0] ? why : "The extractor stopped without saying why.") +
                " The .nsp was " + nsp + (update.empty() ? "" : ", with the update " + update) + ", opened with " + keys +
                ". Is the .nsp the base game 1.0.0 (not DLC), and the update, if any, the 1.0.1 one for it? "
                "Also, is prod.keys from a console whose firmware is as new as the game's? The full log is extract.log in the "
                "data folder; if it keeps happening, report it on GitHub with that file.");
        return false;
    }
    return true;
}

// extracted/ already holds this package's files (same .nsp name and size, update or not, a romfs.bin of the full size):
// extraction rewrites 7+ GB, so it isn't done again. A different package, an interrupted run or damaged files fail it.
static bool extraction_kept(const std::string &nsp, const std::string &update) {
    if (!check_copied_files(g_root).empty() || !file_exists(g_root + "/romfs.bin")) return false;
    auto text = [](const std::string &path) {
        std::string t;
        if (FILE *f = fopen(path.c_str(), "rb")) {
            char b[8192];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) t.append(b, n);
            fclose(f);
        }
        return t;
    };
    const std::string &pkg = update.empty() ? nsp : update;  // (the manifest names the package extracted last)
    struct stat ps, rs;
    if (stat(pkg.c_str(), &ps) != 0 || stat((g_root + "/romfs.bin").c_str(), &rs) != 0) return false;
    size_t slash = pkg.find_last_of("/\\");
    std::string name = slash == std::string::npos ? pkg : pkg.substr(slash + 1);
    std::string manifest = text(g_root + "/extraction-manifest.json"), index = text(g_root + "/index.json");
    bool ok = !manifest.empty() && !index.empty() &&
              extraction_current(manifest.c_str(), index.c_str(), (unsigned long long)rs.st_size, name.c_str(),
                                 (unsigned long long)ps.st_size, !update.empty());
    fprintf(stderr, "[launcher] extracted game files for %s: %s\n", name.c_str(),
            ok ? "up to date, kept" : "missing or different, extracting");
    return ok;
}

static void *extract_thread(void *) {
    stop_background_prep();
    std::string src;
    pthread_mutex_lock(&g_lock);
    src = g_src;
    pthread_mutex_unlock(&g_lock);
    g_cancel_build = false;
    g_done = 0, g_total = 0, g_phase_base = 0, g_build_frac = -1, g_copy_skipped = false;
    game_files_changed();
    std::string exefs, romfs, nsp, update, found_text;
    bool unusable = false;  // (game packages rather than its files)
    if (src.empty() && check_copied_files(g_root).empty()) {  // copied already (an update needs new game code)
        g_copy_skipped = true;
        if (!build_game_code()) goto done;
        goto check;
    }
    set_job("Looking for the game files...");
    {
        GameFind *f = new GameFind;
        find_game(src, *f, &found_text);
        exefs = f->exefs, romfs = f->romfs, nsp = f->nsp, update = f->update, unusable = f->unusable;
        delete f;
    }
    if (update.empty()) {  // (none next to the base game: the one the player chose, if any)
        pthread_mutex_lock(&g_lock);
        update = g_update_pick;
        pthread_mutex_unlock(&g_lock);
    }
    if ((exefs.empty() || romfs.empty()) && nsp.empty()) {
        set_failure(unusable ? FAIL_UNUSABLE : FAIL_NOT_FOUND, -1, found_text.c_str());
        goto done;
    }
    if (!nsp.empty() && (exefs.empty() || romfs.empty())) {  // a game package: extract it in place
        if (!extraction_kept(nsp, update) && !extract_nsp(nsp, update)) goto done;
        if (!build_id_ok(g_root + "/exefs")) { set_failure(FAIL_VERSION); goto done; }
        g_copy_skipped = true;  // (nothing was copied: the build's bar is the whole progress)
        pthread_mutex_lock(&g_lock);
        g_src.clear();
        pthread_mutex_unlock(&g_lock);
        if (!build_game_code()) goto done;
        goto check;
    }
    if (!build_id_ok(exefs)) { set_failure(FAIL_VERSION); goto done; }
    if (int want = game_romfs_files(game_version_of_main((exefs + "/main").c_str()))) {
        if (int have = count_files(romfs); have < want) {  // (an incomplete copy: refused before copying it)
            fprintf(stderr, "[launcher] %s: %d of %d romfs files\n", romfs.c_str(), have, want);
            set_failure(FAIL_MISSING, have, NULL, want);
            goto done;
        }
    }
    {
        // (the picked folder can't be SkywardRecomp's own copy: setting up again deletes that first)
        char a[4096], b[4096];
        mkdir(g_root.c_str(), 0755);
        if (realpath(exefs.c_str(), a) && realpath(g_root.c_str(), b) && !strncmp(a, b, strlen(b)) &&
            (a[strlen(b)] == '/' || a[strlen(b)] == '\\' || !a[strlen(b)])) {
            set_failure(FAIL_NOT_FOUND);
            goto done;
        }
        rt_remove_game_files(g_root.c_str());  // (an earlier or interrupted setup's; removes the folder too)
        game_files_changed();
        mkdir(g_root.c_str(), 0755);
        unsigned long long rbytes = romfs_dir_bytes(romfs.c_str());
        g_total = rbytes;
        set_job("Copying the game files...");
        if (!romfs_copy_tree(exefs.c_str(), (g_root + "/exefs").c_str(), NULL) ||
            !romfs_copy_tree(romfs.c_str(), (g_root + "/romfs").c_str(), setup_progress)) {
            set_failure(FAIL_COPY);
            goto done;
        }
        g_phase_base = rbytes, g_done = rbytes;
        // Retrying a cancelled compilation must use the copied files and their cache.
        pthread_mutex_lock(&g_lock);
        g_src.clear();
        pthread_mutex_unlock(&g_lock);
        if (!build_game_code()) goto done;
    }
check:
    {
        game_files_changed();
        std::string why = check_game_files(g_root);
        if (why.empty() && !g_headless) prepare_shaders();  // (the GPU device is up: the start screen draws with it)
        if (why.empty() && files_changed(g_root)) set_job(CHANGED_HEAD, true, CHANGED_HINT);  // (playable all the same)
        else if (why.empty()) set_job("Game files ready.");
        else if (why.find("version") != std::string::npos) set_failure(FAIL_VERSION);
        else if (why == "Game code not built yet") set_failure(FAIL_BUILD);  // (not missing files)
        else set_failure(FAIL_MISSING, g_romfs_count, NULL, romfs_want(g_root));
    }
done:
    pthread_mutex_lock(&g_lock);
    g_busy = false;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}
// The game files copied already: only the game code built again (an update, Rebuild game code), with the bar
static void start_game_code_update(void) {
    pthread_mutex_lock(&g_lock);
    g_src.clear();
    g_busy = true;
    pthread_mutex_unlock(&g_lock);
    g_auto_update = true;
    pthread_t th;
    pthread_create(&th, NULL, extract_thread, NULL);
    pthread_detach(th);
}

// RecompLauncher's setup step (main.c --prepare): the start screen's setup without a window. game: the player's copy
// (a folder with the game files, or a base .nsp; NULL: the game files already set up, only the game code built again,
// e.g. after an update); keys: their prod.keys (NULL: next to the package, as on the start screen). Progress as
// "progress <0..1> <step>" lines on out. 0: ready to play; 1: it failed (why on out, details in the log); 2: no copy
// given and none set up.
extern "C" int launcher_prepare(const char *root, const char *game, const char *keys, FILE *out) {
    g_root = root;
    g_src = game ? game : "";
    g_keys_pick = keys ? keys : "";
    g_headless = true;
    if (g_src.empty() && !check_copied_files(g_root).empty()) {
        fprintf(out, "No game files set up yet: pass your copy with --game <folder or .nsp>\n");
        fflush(out);
        return 2;
    }
    fprintf(stderr, "[launcher] prepare: %s (keys: %s)\n", g_src.empty() ? "the game files already set up" : g_src.c_str(),
            g_keys_pick.empty() ? "next to the package" : g_keys_pick.c_str());
    g_busy = true;
    pthread_t th;
    pthread_create(&th, NULL, extract_thread, NULL);
    std::string shown;
    int shown_pct = -1;
    for (bool busy = true; busy;) {
        SDL_Delay(250);
        pthread_mutex_lock(&g_lock);
        busy = g_busy;
        std::string job = g_job;
        bool failed = g_job_failed;
        pthread_mutex_unlock(&g_lock);
        int pct = (int)(extract_progress() * 100);
        if (!failed && !job.empty() && (job != shown || pct != shown_pct)) {
            fprintf(out, "progress %.2f %s\n", pct / 100.0, job.c_str());
            fflush(out);
            shown = job, shown_pct = pct;
        }
    }
    pthread_join(th, NULL);
    bool changed = g_job == CHANGED_HEAD;  // (playable all the same, as on the start screen)
    if (g_job_failed && !changed) {
        fprintf(out, "%s. %s\n", g_job.c_str(), g_job_hint.c_str());
        fflush(out);
        fprintf(stderr, "[launcher] prepare failed: %s. %s [%s]\n", g_job.c_str(), g_job_hint.c_str(), g_job_tech.c_str());
        return 1;
    }
    fprintf(out, "progress 1 %s\n", changed ? CHANGED_HEAD : "Game files ready");
    fflush(out);
    return 0;
}

// ------------------------------------------------------------------ folder dialog (SDL calls back on any thread)
// files NULL: no window could be shown (an empty list is a cancel). Linux shows it through the desktop's file chooser
// portal or zenity, and a desktop with neither (a bare window manager) has none: say so instead of Choose doing nothing.
static const char *const PICKER_FAILED = "Couldn't open a folder window";
static std::string picker_hint(const char *what) {
    std::string err = SDL_GetError();
    fprintf(stderr, "[launcher] %s window: %s\n", what, err.c_str());
#ifdef __linux__
    return std::string("SkywardRecomp opens the ") + what + " window through your desktop's file chooser "
           "(xdg-desktop-portal) or zenity. Install either one, then try again.";
#else
    return err;
#endif
}

static void SDLCALL picked(void *, const char *const *files, int) {
    if (!files) { set_job(PICKER_FAILED, true, picker_hint("folder")); return; }
    if (!files[0]) return;
    std::string found;  // game_find_describe(found): what the folder has and why
    bool base = false, update = false, keys = false, nsp = false;
    {
        GameFind *f = new GameFind;
        find_game(files[0], *f, &found);
        note_pick_context(*f);
        base = (f->exefs[0] && f->romfs[0]) || f->nsp[0] != 0;
        nsp = f->nsp[0] != 0;
        update = f->update[0] != 0;
        delete f;
    }
    keys = !find_prod_keys(files[0]).empty();
    pthread_mutex_lock(&g_lock);
    g_src = files[0];
    g_found = found;
    g_scan_base = base;
    g_scan_update = update;
    g_scan_keys = keys;
    g_scan_nsp = nsp;
    g_update_pick.clear(), g_keys_pick.clear();  // (a pick made for the earlier folder)
    g_copied_ok = -1;
    if (g_job == PICKER_FAILED) g_job.clear(), g_job_hint.clear(), g_job_failed = false;
    pthread_mutex_unlock(&g_lock);
}

// A chosen prod.keys: checked now, so a wrong file is refused here instead of failing the extraction later.
static void SDLCALL picked_keys(void *, const char *const *files, int) {
    if (!files) { set_job(PICKER_FAILED, true, picker_hint("file")); return; }
    if (!files[0]) return;
    if (!looks_like_prod_keys(files[0])) {
        set_job("That file isn't a prod.keys", true,
                "prod.keys is a text file of lines like \"header_key = ...\", dumped from your own console with Lockpick_RCM. "
                "Choose that file.");
        return;
    }
    pthread_mutex_lock(&g_lock);
    g_keys_pick = files[0];
    if (g_job_failed && g_job.find("prod.keys") != std::string::npos) g_job.clear(), g_job_hint.clear(), g_job_failed = false;
    pthread_mutex_unlock(&g_lock);
}

// A chosen update package: it must be the 1.0.1 update (a .nsp that says so), refused here with its reason otherwise.
static void SDLCALL picked_update(void *, const char *const *files, int) {
    if (!files) { set_job(PICKER_FAILED, true, picker_hint("file")); return; }
    if (!files[0]) return;
    char why[200] = "";
    if (game_nsp_classify(files[0], why, sizeof why) != GAME_NSP_UPDATE) {
        set_job("That isn't the 1.0.1 update package", true,
                std::string(why[0] ? std::string(why) + " " : "") +
                "Choose the update .nsp of Skyward Sword HD (title id 01002DA013484800, \"[v65536]\"), or leave this empty "
                "if it's in the game files folder.");
        return;
    }
    pthread_mutex_lock(&g_lock);
    g_update_pick = files[0];
    if (g_job_failed && g_job.find("update package") != std::string::npos) g_job.clear(), g_job_hint.clear(), g_job_failed = false;
    pthread_mutex_unlock(&g_lock);
}

// ------------------------------------------------------------------ OneDrive (Windows)
// A data root inside a OneDrive folder uploads every game file to the player's OneDrive: a warning on the start screen
// with roughly how much, and Choose another folder (the Data folder's Change...: move the data there) or Keep here
// (settings.onedrive_kept). Checked once per launch on the current data root (plat_onedrive_synced); the
// size in the background: the data root's own once set up, before that the picked game files (or a typical copy) plus
// what the setup adds. LARECOMP_TEST_ONEDRIVE=1 (test): shown on any system.
#define GAME_FILES_GB 6.3   // exefs + romfs, as players bring them
#define SETUP_ADDS_GB 0.4   // the built game code and its kept pieces (0.23, a beta_5 setup), shader caches
static int g_onedrive = -1;                // -1: not checked yet
static std::atomic<double> g_od_gb{-1};    // the estimate (-1: not known yet)
static std::atomic<int> g_od_job;
static std::string g_od_for = "\x02";      // what the estimate is for: "" set up, "\x01" nothing picked, else the folder

static void *onedrive_size_thread(void *arg) {
    std::string *src = (std::string *)arg;
    int job = g_od_job;
    double gb;
    if (src->empty()) gb = rt_tree_size(rt_data_root()) / 1e9;
    else {
        GameFind *f = new GameFind;
        if (*src != "\x01") game_find(src->c_str(), exefs_supported, f);
        std::string exefs = f->exefs, romfs = f->romfs, nsp = f->nsp;
        delete f;
        if (!nsp.empty()) {  // (a game package: the extraction writes about its own size)
            struct stat st;
            gb = (stat(nsp.c_str(), &st) == 0 ? (double)st.st_size * 1.05 / 1e9 : GAME_FILES_GB) + SETUP_ADDS_GB;
        } else {
            gb = (exefs.empty() || romfs.empty() ? GAME_FILES_GB
                                                 : (rt_tree_size(exefs.c_str()) + rt_tree_size(romfs.c_str())) / 1e9)
                 + SETUP_ADDS_GB;
        }
    }
    if (job == g_od_job) g_od_gb = gb;
    delete src;
    return NULL;
}

// The warning, w wide at the cursor (the start screen's upper right; before the setup, under the Game Files rows).
// 1: Keep here pressed, 2: Choose another folder (offered when the data folder can be changed and nothing is running)
static int onedrive_notice(float s, float w, bool ready) {
    std::string src;
    pthread_mutex_lock(&g_lock);
    src = ready ? "" : g_src.empty() ? "\x01" : g_src;
    pthread_mutex_unlock(&g_lock);
    if (src != g_od_for) {  // (set up, or another folder picked: estimate again)
        g_od_for = src, g_od_job++;
        pthread_t th;
        if (pthread_create(&th, NULL, onedrive_size_thread, new std::string(src)) == 0) pthread_detach(th);
    }
    double gb = g_od_gb;
    if (gb < 0) return 0;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, PAL_PANEL_DARK_V4(0.86f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s, 18 * s));
    ImGui::BeginChild("##onedrive", ImVec2(w, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NavFlattened);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(PAL_WARN_V4, "!  This folder is synced by OneDrive");
    ImGui::TextColored(PAL_TEXT2_V4, "All game files (about %.0f GB) will be uploaded to your OneDrive.",
                       fmax(1.0, gb));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4 * s));
    int r = 0;
    pthread_mutex_lock(&g_lock);
    bool busy = g_busy;
    pthread_mutex_unlock(&g_lock);
    if (!rt_data_root_fixed()) {
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Choose another folder")) r = 2;
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button("Keep here")) r = 1;
    ImGui::EndChild();
    return r;
}
static void onedrive_keep(void) {
    g_settings.onedrive_kept = true;
    settings_save();
    fprintf(stderr, "[launcher] OneDrive warning: kept here\n");
}

// ------------------------------------------------------------------ the user's own Nintendo fonts (not shipped)
// runtime/hle/pl.c loads fonts/ first, else a close substitute of the requested language. Import copies recognized
// files only: the console's own names for the shared fonts (Standard, Chinese Simplified / its Ext, Chinese
// Traditional, Korean and NintendoExt).
static const char *FONT_STD[] = {"nintendo_udsg-r_std_003.bfttf", "nintendo_udsg-r_std_003.ttf",
                                 "nintendo_udsgr_std_003.ttf", "nintendo_udsgr_std_003.bfttf"};
static const char *FONT_LANG[] = {"nintendo_udsg-r_org_zh-cn_003.bfttf", "nintendo_udsg-r_org_zh-cn_003.ttf",
                                  "nintendo_udsg-r_ext_zh-cn_003.bfttf", "nintendo_udsg-r_ext_zh-cn_003.ttf",
                                  "nintendo_udjxh-db_zh-tw_003.bfttf", "nintendo_udjxh-db_zh-tw_003.ttf",
                                  "nintendo_udsg-r_ko_003.bfttf", "nintendo_udsg-r_ko_003.ttf"};
static const char *FONT_EXT[] = {"nintendo_ext_003.bfttf", "nintendo_ext_003.ttf"};
static std::vector<std::string> g_font_picks;
static std::string g_font_msg, g_font_err;  // (g_font_err: the file window couldn't open; from the dialog's thread)

static bool have_font(const char *const *names, int n) {
    for (int i = 0; i < n; i++)
        if (file_exists(std::string("fonts/") + names[i])) return true;
    return false;
}

static void SDLCALL picked_fonts(void *, const char *const *files, int) {
    std::string err = files ? "" : std::string("Couldn't open a file window. ") + picker_hint("file");
    pthread_mutex_lock(&g_lock);
    for (int i = 0; files && files[i]; i++) g_font_picks.push_back(files[i]);
    if (!files) g_font_err = err;
    pthread_mutex_unlock(&g_lock);
}

static void import_fonts() {
    pthread_mutex_lock(&g_lock);
    std::vector<std::string> picks;
    picks.swap(g_font_picks);
    if (!g_font_err.empty()) g_font_msg = g_font_err, g_font_err.clear();
    pthread_mutex_unlock(&g_lock);
    if (picks.empty()) return;
    mkdir("fonts", 0755);
    int ok = 0, skipped = 0;
    for (auto &src : picks) {
        std::string base = src.substr(src.find_last_of("/\\") + 1), low = base;  // (Windows paths use '\\' too)
        for (auto &c : low) c = (char)tolower(c);
        bool known = false;
        for (auto n : FONT_STD) known |= low == n;
        for (auto n : FONT_LANG) known |= low == n;
        for (auto n : FONT_EXT) known |= low == n;
        if (!known) { skipped++; continue; }
        FILE *in = fopen(src.c_str(), "rb"), *out = fopen(("fonts/" + low).c_str(), "wb");
        if (in && out) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
            ok++;
        }
        if (in) fclose(in);
        if (out) fclose(out);
    }
    g_font_msg = std::to_string(ok) + " font file(s) imported" + (skipped ? ", " + std::to_string(skipped) + " not recognized" : "") +
                 ". Used from the next game start.";
}

static void remove_fonts() {
    for (auto n : FONT_STD) unlink((std::string("fonts/") + n).c_str());
    for (auto n : FONT_LANG) unlink((std::string("fonts/") + n).c_str());
    for (auto n : FONT_EXT) unlink((std::string("fonts/") + n).c_str());
    g_font_msg = "Imported fonts removed: the substitute fonts are used.";
}

// ------------------------------------------------------------------ background: a title movie frame
#ifdef __APPLE__
static id<MTLTexture> g_bg;
#define BG_TEXID ((ImTextureID)(__bridge void *)g_bg)
#define ART_TEXID ((ImTextureID)(__bridge void *)g_art)
typedef id<MTLDevice> GpuDevice;
#else
static uint64_t g_bg;  // ImGui texture id (Vulkan descriptor set)
#define BG_TEXID ((ImTextureID)g_bg)
#define ART_TEXID ((ImTextureID)g_art)
typedef void *GpuDevice;
#endif
static int g_bg_w, g_bg_h;
// The user's own picture (launcher.jpg / launcher.png in the data folder; nothing is bundled): behind the start screen instead
// of the title movie frame, and behind the setup page.
static decltype(g_bg) g_art;
static int g_art_w, g_art_h;

// The first frame at or after `at` seconds of a video, or a picture (at <= 0), as a texture.
static void load_frame(GpuDevice dev, const std::string &path, double at, decltype(g_bg) &tex, int &tw, int &th) {
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) return;
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si >= 0 && avformat_find_stream_info(fmt, NULL) >= 0) {
        AVStream *st = fmt->streams[si];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        if (codec && avcodec_open2(cc, codec, NULL) == 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *fr = av_frame_alloc();
            int64_t want = (int64_t)(at / av_q2d(st->time_base));
            if (at > 0) av_seek_frame(fmt, si, want, AVSEEK_FLAG_BACKWARD);
            bool got = false;
            while (!got && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) == 0)
                    while (avcodec_receive_frame(cc, fr) == 0)
                        if (at <= 0 || fr->best_effort_timestamp >= want) { got = true; break; }
                av_packet_unref(pkt);
            }
            if (got) {
                // a big picture is shrunk while converting: the window never shows more than ~2560 px of it, and the
                // pixels (CPU copy, then the texture) cost RAM for nothing
                int w = fr->width, h = fr->height;
                if (w > 2560) h = (int)((int64_t)h * 2560 / w), w = 2560;
                std::vector<uint8_t> rgba((size_t)w * h * 4);
                SwsContext *sws = sws_getContext(fr->width, fr->height, (AVPixelFormat)fr->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                uint8_t *dst[1] = {rgba.data()};
                int dst_ls[1] = {w * 4};
                sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, dst_ls);
                sws_freeContext(sws);
#ifdef __APPLE__
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:w height:h mipmapped:NO];
                tex = [dev newTextureWithDescriptor:td];
                [tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba.data() bytesPerRow:w * 4];
#else
                (void)dev;
                tex = vk_imgui_texture_rgba(rgba.data(), w, h);
#endif
                tw = w, th = h;
            }
            av_frame_free(&fr);
            av_packet_free(&pkt);
        }
        avcodec_free_context(&cc);
    }
    avformat_close_input(&fmt);
}

static void load_background(GpuDevice dev) {
    // SS's opening movie (the LA movie/title.webm does not exist here); 8 s in: past the fade-in from black, a lit scene
    load_frame(dev, g_root + "/romfs/THP/Demo01_01.webm", 8.0, g_bg, g_bg_w, g_bg_h);
}

static void load_art(GpuDevice dev) {
    for (const char *n : {"/launcher.jpg", "/launcher.png"}) {
        std::string p = std::string(rt_data_root()) + n;
        if (file_exists(p)) load_frame(dev, p, 0, g_art, g_art_w, g_art_h);
        if (g_art) return; // user's own picture wins over the bundled art
    }
    if (!g_art) {
        // Bundled Skyward Sword art is the default background. Decode it through
        // the same ffmpeg path as user art, using a short-lived file in data root.
        const std::string p = std::string(rt_data_root()) + "/launcher_backdrop.jpg";
        if (FILE *f = fopen(p.c_str(), "wb")) {
            fwrite(kLauncherBackdropJpg, 1, sizeof(kLauncherBackdropJpg), f);
            fclose(f);
            load_frame(dev, p, 0, g_art, g_art_w, g_art_h);
            remove(p.c_str());
        }
    }
}

// ------------------------------------------------------------------ seeds
static std::vector<std::string> list_seeds() {
    std::vector<std::string> out;
    if (DIR *d = opendir("rando")) {
        while (struct dirent *e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string dir = std::string("rando/") + e->d_name;
            if (file_exists(dir + "/settings.json") && file_exists(dir + "/romfs")) out.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------------ screen
static ImFont *g_font_title, *g_font_menu, *g_font_small;
enum Page { MAIN, SETTINGS, SETUP };  // SETTINGS: the in-game menu's tabs (runtime/ui/menu.mm)
static int g_refocus = MAIN;  // back on the main page: select the item of the page we came from (controllers)

// A big menu line: gold + a marker when focused / hovered (keyboard, gamepad or mouse), grey when disabled
static bool g_menu_small;  // sub pages: buttons in the small font
void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float));  // menu.mm
void menu_tabs_focus(bool first_tab);
extern int g_tab_request_ext;
extern "C" void menu_apply_window_requests(void *sdl_window);
extern "C" void input_cursor_event(const SDL_Event *e);  // runtime/hle/input.c
extern "C" bool input_cursor_hidden(void);
extern "C" int g_start_screen;  // runtime/hle/warp.c: the start screen is up (no game loaded: don't read its memory)
// The Randomizer tab's Play / Play Now on the start screen (rando_tab.mm): start the game with that seed
static std::string g_play_seed;
static bool g_play_seed_req, g_play_pending;
extern "C" void launcher_play_seed(const char *seed) {
    g_play_seed = seed ? seed : "";
    g_play_seed_req = true;
}
static float g_menu_gap = 0.35f;  // space under each item, in line heights

// The first PLAY ever (settings.menu_tip_seen): a note on opening the SkywardRecomp menu in game (it isn't obvious),
// OK starts the game. Returns true once OK is pressed.
static bool g_tip_test;  // (LARECOMP_LAUNCHER_SHOT=<frame>:tip)
// First PLAY ever, before the menu note: authentic 16:9 or the unlocked (fill the window) aspect ratio.
static bool aspect_prompt(float s) {
    bool done = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PAL_PANEL_DARK_V4(0.94f));
    bool open = ImGui::BeginPopupModal("##aspect", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("How would you like to play?");
        ImGui::Spacing();
        ImGui::TextDisabled("You can change this any time in Settings.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        float bw = 300 * s, gap = 20 * s;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 2 * bw - gap) * 0.5f);
        int pick = -1;
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Authentic 16:9", ImVec2(bw, 44 * s))) pick = ASPECT_ORIGINAL;
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Unlocked (fill the screen)", ImVec2(bw, 44 * s))) pick = ASPECT_UNLOCKED;
        if (pick >= 0) {
            g_settings.aspect = pick;
            g_settings.aspect_asked = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            done = true;
        }
        ImGui::EndPopup();
    }
    return done;
}

static bool menu_tip(float s) {
    bool go = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PAL_PANEL_DARK_V4(0.94f));
    bool open = ImGui::BeginPopupModal("##menutip", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        // the controller's home button first (what players reach for: the PS button on a DualSense), the combo second
        static const char *const home[] = {"Home button", "Xbox button", "PlayStation button"};  // (bind_label_family)
        static const char *const pad[] = {"Minus + Plus", "View + Menu", "Create + Options"};
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("SkywardRecomp has its own menu for settings, controls and more.");
        ImGui::Spacing();
        int fam = bind_label_family() % 3;
        ImGui::Text("Open it in game with F1 or Esc, or on a controller with the %s (or %s together).", home[fam], pad[fam]);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 180 * s) * 0.5f);
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("OK", ImVec2(180 * s, 44 * s)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            g_settings.menu_tip_seen = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            go = true;
        }
        ImGui::EndPopup();
    }
    return go;
}

// The game files page's example: a Finder-style window (the same on every platform) with the folder to choose -
// "Game files", opened: exefs in it opened (its files), romfs closed. All drawn here (shapes and the page's font, sized from the font: sharp at
// any scale), up to w wide, at the cursor; it takes its space with a Dummy.
enum { FOLDER_EXAMPLE_ROWS = 9 };
static float folder_example_height(void) {  // (its window, without the Dummy's gap under it)
    float fs = ImGui::GetFontSize();
    return fs * 1.45f + fs * 0.35f * 2 + FOLDER_EXAMPLE_ROWS * fs * 1.12f;
}
static void draw_folder_example(float w) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *font = ImGui::GetFont();
    float fs = ImGui::GetFontSize(), ts = fs * 0.82f;  // (the rows' text a little under the page's)
    float rh = fs * 1.12f, bar = fs * 1.45f, pad = fs * 0.35f, r = fs * 0.45f;
    struct Item { const char *name; int depth; int kind; };  // kind: 0 file, 1 folder opened, 2 folder closed
    static const Item items[] = {{"Game files", 0, 1}, {"exefs", 1, 1}, {"main", 2, 0}, {"main.npdm", 2, 0},
                                 {"rtld", 2, 0}, {"sdk", 2, 0}, {"subsdk0", 2, 0}, {"subsdk1", 2, 0}, {"romfs", 1, 2}};
    const int n = FOLDER_EXAMPLE_ROWS;
    static_assert(sizeof items / sizeof items[0] == FOLDER_EXAMPLE_ROWS, "one row per item");
    w = fminf(w, fs * 15);
    float h = folder_example_height();
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
    const ImU32 body = IM_COL32(14, 32, 66, 255), title = IM_COL32(26, 52, 100, 255), edge = IM_COL32(255, 255, 255, 38),
                text = PAL_TEXT, dim = PAL_DIM,
                gold = PAL_ACCENT, gold_tab = IM_COL32(90, 170, 235, 255),
                paper = IM_COL32(222, 234, 250, 255), fold = IM_COL32(140, 165, 205, 255),
                select = IM_COL32(32, 84, 176, 255), text_sel = IM_COL32(250, 250, 255, 255);
    dl->AddRectFilled(ImVec2(a.x, a.y + fs * 0.12f), ImVec2(b.x, b.y + fs * 0.22f), IM_COL32(0, 0, 0, 70), r);  // shadow
    dl->AddRectFilled(a, b, body, r);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + bar), title, r, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(a.x, a.y + bar), ImVec2(b.x, a.y + bar), IM_COL32(0, 0, 0, 90), 1.0f);
    dl->AddRect(a, b, edge, r, 0, 1.0f);
    // the title bar: three dots on the left, the folder it's in (a typical one) in the middle
    static const ImU32 dots[] = {IM_COL32(120, 175, 235, 255), IM_COL32(120, 175, 235, 255), IM_COL32(120, 175, 235, 255)};
    float dr = fs * 0.21f, cy = a.y + bar * 0.5f;
    for (int i = 0; i < 3; i++) dl->AddCircleFilled(ImVec2(a.x + fs * 0.65f + i * fs * 0.62f, cy), dr, dots[i]);
    const char *t = "Documents";
    ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0, t);
    dl->AddText(font, ts, ImVec2(a.x + (w - tsz.x) * 0.5f, cy - tsz.y * 0.5f), text, t);
    // the list: a disclosure triangle on folders, the folder / file glyph, the name
    float g = fs * 0.78f;  // (glyph size)
    for (int i = 0; i < n; i++) {
        const Item &it = items[i];
        float y = a.y + bar + pad + i * rh, my = y + rh * 0.5f;
        if (i == 0)  // the folder to choose: selected, as Finder shows it
            dl->AddRectFilled(ImVec2(a.x + fs * 0.25f, y), ImVec2(b.x - fs * 0.25f, y + rh), select, fs * 0.25f);
        else if (i & 1) dl->AddRectFilled(ImVec2(a.x + 1, y), ImVec2(b.x - 1, y + rh), IM_COL32(255, 255, 255, 9));
        float x = a.x + fs * 0.5f + it.depth * fs * 1.25f;
        if (it.kind) {  // ▼ opened, ▶ closed
            float u = fs * 0.2f, cx = x + u;
            ImU32 tc = i == 0 ? text_sel : dim;
            if (it.kind == 1)
                dl->AddTriangleFilled(ImVec2(cx - u, my - u * 0.6f), ImVec2(cx + u, my - u * 0.6f),
                                      ImVec2(cx, my + u * 0.75f), tc);
            else
                dl->AddTriangleFilled(ImVec2(cx - u * 0.6f, my - u), ImVec2(cx - u * 0.6f, my + u),
                                      ImVec2(cx + u * 0.75f, my), tc);
        }
        x += fs * 0.6f;
        float gx = x, gy = my - g * 0.5f;
        if (it.kind) {  // folder: a tab, then the body over it
            dl->AddRectFilled(ImVec2(gx, gy + g * 0.12f), ImVec2(gx + g * 0.46f, gy + g * 0.42f), gold_tab, g * 0.1f);
            dl->AddRectFilled(ImVec2(gx, gy + g * 0.26f), ImVec2(gx + g, gy + g * 0.9f), gold, g * 0.1f);
        } else {  // file: a page with its top right corner folded
            float l = gx + g * 0.16f, rr = gx + g * 0.84f, tp = gy + g * 0.02f, bt = gy + g * 0.98f, c = g * 0.24f;
            ImVec2 pts[] = {ImVec2(l, tp), ImVec2(rr - c, tp), ImVec2(rr, tp + c), ImVec2(rr, bt), ImVec2(l, bt)};
            dl->AddConvexPolyFilled(pts, 5, paper);
            dl->AddTriangleFilled(ImVec2(rr - c, tp), ImVec2(rr - c, tp + c), ImVec2(rr, tp + c), fold);
        }
        ImVec2 nsz = font->CalcTextSizeA(ts, FLT_MAX, 0, it.name);
        float nx = x + g + fs * 0.4f;
        dl->AddText(font, ts, ImVec2(nx, my - nsz.y * 0.5f), i == 0 ? text_sel : text, it.name);
        if (i == 0) {  // an arrow pointing back at the name, then what to do (smaller when the row is narrow)
            const char *hint = "Choose this one";
            float ax = nx + nsz.x + fs * 0.5f, aw = fs * 1.1f, gap = fs * 0.3f, room = b.x - fs * 0.6f - ax;
            float hs = ts, hw = font->CalcTextSizeA(hs, FLT_MAX, 0, hint).x;
            if (aw + gap + hw > room) hs *= fmaxf(0.7f, (room - aw - gap) / hw);
            float hx = ax + aw + gap, th = fs * 0.09f, hd = fs * 0.26f;
            dl->AddLine(ImVec2(ax + th, my), ImVec2(ax + aw, my), gold, th);
            dl->AddTriangleFilled(ImVec2(ax - th, my), ImVec2(ax + hd, my - hd), ImVec2(ax + hd, my + hd), gold);
            dl->AddText(font, hs, ImVec2(hx, my - font->CalcTextSizeA(hs, FLT_MAX, 0, hint).y * 0.5f), gold, hint);
        }
    }
    ImGui::Dummy(ImVec2(w, h));
}

// Start-screen text: a soft drop shadow, then the text with a top-to-bottom colour gradient (top == bottom: flat)
static void text_fx(ImDrawList *dl, ImFont *font, float size, ImVec2 p, ImU32 top, ImU32 bottom, const char *t,
                    float shadow) {
    if (shadow > 0)
        for (float d : {shadow, shadow * 0.5f}) dl->AddText(font, size, ImVec2(p.x + d, p.y + d), IM_COL32(4, 8, 30, 110), t);
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, size, p, top, t);
    if (top == bottom) return;
    float y0 = p.y + size * 0.2f, y1 = p.y + size * 0.85f;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) {
        float f = (dl->VtxBuffer[i].pos.y - y0) / (y1 - y0);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        dl->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
                                                                     a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f));
    }
}

// A four-point sparkle (PLAY's twinkle): k = 0..1 grows and fades it
static void sparkle(ImDrawList *dl, ImVec2 c, float r, float k, float spin) {
    if (k <= 0) return;
    ImVec2 pts[8];
    for (int i = 0; i < 8; i++) {
        float a = spin + i * 3.14159265f / 4, rr = (i & 1 ? r * 0.22f : r) * k;
        pts[i] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    dl->AddCircleFilled(c, r * 0.55f * k, IM_COL32(190, 230, 255, (int)(60 * k)));  // soft glow
    dl->AddConvexPolyFilled(pts, 8, IM_COL32(235, 248, 255, (int)(235 * k)));
}

// split .. split_end: that part of the label in its own colour (PLAY's choice: Vanilla in vanilla cream)
static bool menu_item(const char *label, bool enabled, bool *hovered = NULL, const char *split = NULL, ImU32 col2 = 0,
                      const char *split_end = NULL) {
    ImGui::PushFont(g_menu_small ? g_font_small : g_font_menu);
    ImGui::BeginDisabled(!enabled);
    ImVec2 at = ImGui::GetCursorScreenPos();
    // (the ID from the label's fixed part: PLAY's changes with the seed shown, and a new ID loses the selection)
    ImGui::PushID(label, split ? split : label + strlen(label));
    ImVec2 sz = ImGui::CalcTextSize(label);
    // (EnableNav: otherwise an InvisibleButton can't be reached with the d-pad / arrow keys - ImGui 1.91)
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the hover band shows the selection, no box)
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(sz.x + 40, sz.y + 6), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    bool hot = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) *hovered = hot;
    ImU32 col = !enabled ? IM_COL32(170, 175, 200, 140) : hot ? PAL_ACCENT : IM_COL32(250, 250, 255, 255);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (hot && enabled) {  // a shadow band behind the item (like the version corner's), feathered, with a warm glow
        float x0 = ImGui::GetWindowPos().x, x1 = at.x + sz.x * 0.75f, x2 = at.x + sz.x + sz.y * 3.5f;
        float f = sz.y * 0.22f, y0 = at.y - f * 0.4f, y1 = y0 + f, y3 = at.y + sz.y + f * 0.2f, y2 = y3 - f;
        ImU32 c = IM_COL32(6, 10, 34, 150), g = IM_COL32(120, 200, 255, 34), n = IM_COL32(6, 10, 34, 0), ng = IM_COL32(120, 200, 255, 0);
        for (int pass = 0; pass < 2; pass++) {  // dark band, then the glow over it
            ImU32 a = pass ? g : c, z = pass ? ng : n;
            dl->AddRectFilledMultiColor(ImVec2(x0, y1), ImVec2(x1, y2), a, a, a, a);
            dl->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, y2), a, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), z, z, a, a);  // (feathered top / bottom)
            dl->AddRectFilledMultiColor(ImVec2(x1, y0), ImVec2(x2, y1), z, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y2), ImVec2(x1, y3), a, a, z, z);
            dl->AddRectFilledMultiColor(ImVec2(x1, y2), ImVec2(x2, y3), a, z, z, z);
        }
    }
    ImU32 col_b = hot && enabled ? IM_COL32(90, 170, 245, 255) : col;
    if (split && enabled) {
        std::string head(label, split - label);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, head.c_str(), sz.y * 0.05f);
        std::string mid = split_end ? std::string(split, split_end - split) : std::string(split);
        ImVec2 p2(at.x + ImGui::CalcTextSize(label, split).x, at.y);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p2, col2, col2, mid.c_str(), sz.y * 0.05f);
        if (split_end)
            text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + ImGui::CalcTextSize(label, split_end).x, at.y),
                    col, col, split_end, sz.y * 0.05f);
    } else
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, label, enabled ? sz.y * 0.05f : 0);
    if (g_menu_gap >= 0) ImGui::Dummy(ImVec2(0, sz.y * g_menu_gap));  // line spacing
    else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sz.y * g_menu_gap);  // (tighter than the font's line height)
    ImGui::EndDisabled();
    ImGui::PopFont();
    return clicked;
}


// SKY-36 Home: one primary action. These accessors are the only place that reads readiness for the
// home screen; they use the same file checks as the rest of the launcher. The shared LauncherState
// (SKY-31) can replace the bodies without touching any of the drawing below.
static bool home_ready(const std::string &status) { return status.empty(); }

static const char *home_status_chip(const std::string &status, bool busy) {
    if (busy) return "Setting up";
    return home_ready(status) ? "Ready" : "Setup needed";
}

static const char *home_action_label(const std::string &status) {
    return home_ready(status) ? "Play" : "Set Up Game";
}

// The copied files' release, cached on the same counter as the old start-screen label: detecting
// it hashes npdm, so it must not run every frame.
static std::string home_game_version(void) {
    static std::string label;
    static int label_of = -1;
    const int changes = g_files_changes.load();
    if (label_of != changes) {
        label_of = changes;
        char buf[32] = "";
        const char *v = game_release_label((g_root + "/exefs").c_str(), buf, sizeof buf);
        label = v ? v : "";
    }
    return label;
}

// A status chip, not a control: short text on a pill, so the action stays the only big thing.
static void draw_home_chip(const char *text, bool ready, float s) {
    ImGui::PushFont(g_font_small);
    ImVec2 at = ImGui::GetCursorScreenPos();
    float pad = 12 * s, h = 28 * s;
    float dot = h * 0.5f, w = ImGui::CalcTextSize(text).x + 2 * pad + dot;  // (a status dot, then the one line)
    ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + w, at.y + h),
                                              ready ? IM_COL32(32, 120, 190, 235) : IM_COL32(40, 64, 104, 230),
                                              h * 0.5f);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + pad + dot * 0.3f, at.y + h * 0.5f), h * 0.15f, ready ? PAL_OK : IM_COL32(250, 205, 125, 255));
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad + dot, at.y + (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", text);
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + h));
    ImGui::PopFont();
}

// The one dominant control: a large rounded button. menu_item keeps the existing mouse, keyboard
// and gamepad activation; the rectangle only makes its weight match its importance.
static bool draw_home_primary(const char *label, float s, bool *hovered, bool enabled) {
    ImGui::PushFont(g_font_menu);
    ImVec2 at = ImGui::GetCursorScreenPos();
    float pad = 30 * s, h = 74 * s;
    ImVec2 size(std::max(360 * s, ImGui::CalcTextSize(label).x + 2 * pad), h);
    ImVec2 m = ImGui::GetIO().MousePos;
    bool hot = enabled && m.x >= at.x && m.x <= at.x + size.x && m.y >= at.y && m.y <= at.y + size.y;
    ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
                                              !enabled ? PAL_SKY_OFF
                                              : hot ? PAL_SKY_HI : PAL_SKY,
                                              12 * s);
    ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + size.x, at.y + size.y),
                                        PAL_ACCENT_A(170), 12 * s, 0, 2 * s);
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + (h - ImGui::GetFontSize()) * 0.5f));
    bool clicked = menu_item(label, enabled, hovered);
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + size.y));
    ImGui::PopFont();
    return clicked;
}

// Vanilla / seed stays reachable, but as a line under the button rather than another menu row.
static int draw_home_seed(float s, float center_x, const std::vector<std::string> &choices, int choice) {
    if (choices.empty()) return 0;
    const char *lt = "\xe2\x80\xb9", *rt = "\xe2\x80\xba";
    std::string line = std::string(lt) + "  " + choices[choice] + "  " + rt;
    ImGui::PushFont(g_font_small);
    ImVec2 size = ImGui::CalcTextSize(line.c_str());
    ImVec2 at(center_x - size.x * 0.5f, ImGui::GetCursorScreenPos().y);
    ImGui::SetCursorScreenPos(at);
    ImGui::TextColored(PAL_TEXT2_V4, "%s", line.c_str());
    ImVec2 lsz = ImGui::CalcTextSize(lt), rsz = ImGui::CalcTextSize(rt);
    float lx = at.x, rx = at.x + size.x - rsz.x, pad = 10 * s;
    ImVec2 m = ImGui::GetIO().MousePos;
    int arrow = 0;
    if (m.y >= at.y - pad && m.y <= at.y + size.y + pad) {
        if (m.x >= lx - pad && m.x <= lx + lsz.x + pad) arrow = -1;
        else if (m.x >= rx - pad && m.x <= rx + rsz.x + pad) arrow = 1;
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + size.y));
    ImGui::PopFont();
    return arrow;
}

// Controllers, as in the in-game menu (runtime/ui/menu.mm): read straight from SDL, confirm / back as the Confirm
// button setting says (pad_nav.h: the game's layout, or the controller's convention); d-pad / left stick move.
// (ImGui's own SDL gamepad handling is switched off: per the user, it didn't work on the start screen.)

static void launcher_pad_open_all(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

// true when the event was a controller event (handled here)
static bool launcher_pad_event(const SDL_Event &e) {
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        ImGuiKey k = ui_pad_key(e.gbutton.which, e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.0f;
        // the left stick also works as the d-pad (ImGui moves between items with the d-pad only; the stick scrolls):
        // pressed past half way, released under 0.3
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            static bool held[4];  // left, right, up, down
            bool x = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
            int neg = x ? 0 : 2, pos = neg + 1;
            const ImGuiKey keys[4] = {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp,
                                      ImGuiKey_GamepadDpadDown};
            for (int i : {neg, pos}) {
                float m = i == neg ? -v : v;
                bool now = held[i] ? m > 0.3f : m > 0.5f;
                if (now != held[i]) { held[i] = now; io.AddKeyEvent(keys[i], now); }
            }
        }
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, v > 0.3f, v > 0.3f ? v : 0);
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, v > 0.3f, v > 0.3f ? v : 0);
        }
        return true;
    }
    default: return false;
    }
}

// Returns when PLAY is chosen (seed stored in g_settings.rando_seed); QUIT exits.
// Settings' first tab, on the start screen only: the extracted game files (size, delete) and the user's own Switch
// fonts (import / back to the substitute)
// Open a folder in Finder / Explorer (relative paths are in the data root, the working directory)
static void reveal_folder(const std::string &dir) {
    std::string path = dir;
    bool abs = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    if (!abs) path = std::string(rt_data_root()) + "/" + path;
    std::string url = "file://";
    if (path[0] != '/') url += "/";  // (Windows: file:///C:/...)
    for (char ch : path) {
        if (ch == '\\') ch = '/';
        if (ch == ' ') url += "%20";
        else if (ch == '%') url += "%25";
        else if (ch == '#') url += "%23";
        else url += ch;
    }
    SDL_OpenURL(url.c_str());
}

static SDL_Window *g_launcher_win;
// The progress overlay in front of the start screen (input waits): the setup, an update's build, or the shader
// preparation (with Skip) - the setup's, or the background one when PLAY was pressed before it finished
static void draw_busy_overlay(ImVec2 ds, const std::string &job, bool test_busy, bool prep_wait = false) {
    float sc = ds.y / 1080.0f;
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    ImVec2 o = ImGui::GetMainViewport()->Pos;
    if (!g_auto_update) fg->AddRectFilled(o, ImVec2(o.x + ds.x, o.y + ds.y), IM_COL32(0, 0, 0, 170));  // (an update: plain)
    float f = test_busy && g_prep_frac < 0 ? 0.42f : prep_wait && g_prep_frac < 0 ? 0.99f : extract_progress();
    // (while the game code is built: what's happening, on whose files - runtime/gamebuild.c)
    bool preparing = g_prep_frac >= 0 || prep_wait, recompiling = g_build_frac >= 0 && !preparing;
    const char *title = preparing ? "Preparing shaders" : g_auto_update ? "Updating the game"
                      : recompiling ? "Recompiling the game" : "Setting up the game files";
    static const char *const notes[] = {"This only needs to be done once and may take a while."};
    int nnotes = recompiling && !g_auto_update ? 1 : 0;
    float tfs = g_font_menu->FontSize * 0.75f, sfs = g_font_small->FontSize * 0.85f;
    ImVec2 ts = g_font_menu->CalcTextSizeA(tfs, FLT_MAX, 0, title);
    float line_h = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, "A").y;
    float pw = fminf(ds.x * 0.6f, 900 * sc), bar_y = 36 * sc + ts.y + 30 * sc;
    for (int i = 0; i < nnotes; i++)  // (wide enough for its text, within the window)
        pw = fmaxf(pw, g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]).x + 96 * sc);
    pw = fminf(fmaxf(pw, ts.x + 96 * sc), ds.x - 32 * sc);
    float ph = bar_y + 24 * sc + 26 * sc + line_h + (nnotes ? 14 * sc + nnotes * line_h : 0) + 40 * sc +
               (preparing ? line_h + 28 * sc : recompiling ? 54 * sc : 0);  // (Skip / Cancel)
    ImVec2 a(o.x + (ds.x - pw) * 0.5f, o.y + (ds.y - ph) * 0.5f), b(a.x + pw, a.y + ph);
    fg->AddRectFilled(a, b, PAL_NAVY, 14 * sc);
    fg->AddRect(a, b, PAL_ACCENT_A(140), 14 * sc, 0, 1.5f * sc);
    fg->AddText(g_font_menu, tfs, ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + 36 * sc), PAL_ACCENT, title);
    ImVec2 ba(a.x + 48 * sc, a.y + bar_y), bb(b.x - 48 * sc, a.y + bar_y + 24 * sc);
    fg->AddRectFilled(ba, bb, IM_COL32(255, 255, 255, 25), 12 * sc);
    if (f > 0) fg->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * f, bb.y), PAL_ACCENT, 12 * sc);
    char line[200];
    if (preparing) snprintf(line, sizeof line, "%d%%", (int)(f * 100));  // (the title says it)
    else snprintf(line, sizeof line, "%s   %d%%", test_busy ? "Setting up the game files..." : job.c_str(),
                  (int)(f * 100));
    ImVec2 ls = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, line);
    fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ls.x) * 0.5f, bb.y + 26 * sc), PAL_TEXT, line);
    for (int i = 0; i < nnotes; i++) {
        ImVec2 ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]);
        fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ns.x) * 0.5f, bb.y + 26 * sc + (i + 1) * line_h + 14 * sc),
                    PAL_DIM, notes[i]);
    }
    if (recompiling) {  // Shader preparation has its own Skip action; cancellation only stops the game-code build.
        ImVec2 ca((a.x + b.x) * 0.5f - 150 * sc, b.y - 66 * sc), cb(ca.x + 300 * sc, ca.y + 44 * sc);
        bool hover = !g_cancel_build && ImGui::IsMouseHoveringRect(ca, cb, false);
        fg->AddRectFilled(ca, cb, hover ? IM_COL32(40, 90, 160, 255) : IM_COL32(24, 52, 100, 255), 8 * sc);
        const char *cancel = g_cancel_build ? "Cancelling..." : "Cancel";  // (B / Circle and Esc too)
        ImVec2 cs = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, cancel);
        fg->AddText(g_font_small, sfs, ImVec2((ca.x + cb.x - cs.x) * 0.5f, (ca.y + cb.y - cs.y) * 0.5f),
                    PAL_ACCENT, cancel);
        if ((hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            g_cancel_build = true;
    }
    if (preparing) {  // Skip: the game then compiles what it needs as it goes (back / Esc too)
        const char *sk = g_prep_skip ? "Skipping..." : "Skip";
        ImVec2 ks = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, sk);
        ImVec2 ka((a.x + b.x - ks.x) * 0.5f - 22 * sc, b.y - 40 * sc - line_h - 10 * sc);
        ImVec2 kb(ka.x + ks.x + 44 * sc, ka.y + line_h + 16 * sc);
        bool hover = !g_prep_skip && !g_prep_skip_ask && ImGui::IsMouseHoveringRect(ka, kb, false);
        fg->AddRect(ka, kb, PAL_ACCENT_A(hover ? 255 : 140), 8 * sc, 0, 1.5f * sc);
        fg->AddText(g_font_small, sfs, ImVec2(ka.x + 22 * sc, ka.y + 8 * sc),
                    hover ? PAL_ACCENT : PAL_TEXT, sk);
        if (!g_prep_skip && !g_prep_skip_ask &&
            ((hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
             ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            g_prep_skip_ask = true;
    }
    if (preparing && g_prep_skip_ask && !g_prep_skip) {  // what Skip means, before it happens
        const char *q = "Shaders will compile as you play, but you may experience some pop-in.";
        const char *yes = "Yes, skip", *no = "No, wait";
        ImVec2 qs = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, q);
        ImVec2 ys = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, yes), ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, no);
        float bw = fmaxf(ys.x, ns.x) + 64 * sc, bh = line_h + 20 * sc, gap = 24 * sc;
        float qw = fminf(fmaxf(qs.x, 2 * bw + gap) + 96 * sc, ds.x - 32 * sc), qh = 40 * sc + line_h + 32 * sc + bh + 36 * sc;
        fg->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), 14 * sc);  // (the bar behind it dimmed)
        ImVec2 qa(o.x + (ds.x - qw) * 0.5f, o.y + (ds.y - qh) * 0.5f), qb(qa.x + qw, qa.y + qh);
        fg->AddRectFilled(qa, qb, PAL_NAVY, 14 * sc);
        fg->AddRect(qa, qb, PAL_ACCENT_A(200), 14 * sc, 0, 1.5f * sc);
        fg->AddText(g_font_small, sfs, ImVec2((qa.x + qb.x - qs.x) * 0.5f, qa.y + 40 * sc), PAL_TEXT, q);
        float by = qb.y - 36 * sc - bh, bx = (qa.x + qb.x) * 0.5f;
        ImVec2 ya(bx - gap * 0.5f - bw, by), yb(bx - gap * 0.5f, by + bh), na(bx + gap * 0.5f, by), nb(bx + gap * 0.5f + bw, by + bh);
        bool hy = ImGui::IsMouseHoveringRect(ya, yb, false), hn = ImGui::IsMouseHoveringRect(na, nb, false);
        fg->AddRect(ya, yb, PAL_ACCENT_A(hy ? 255 : 140), 8 * sc, 0, 1.5f * sc);
        fg->AddRectFilled(na, nb, PAL_ACCENT_A(hn ? 255 : 210), 8 * sc);  // (waiting: the default)
        fg->AddText(g_font_small, sfs, ImVec2((ya.x + yb.x - ys.x) * 0.5f, ya.y + 10 * sc),
                    hy ? PAL_ACCENT : PAL_TEXT, yes);
        fg->AddText(g_font_small, sfs, ImVec2((na.x + nb.x - ns.x) * 0.5f, na.y + 10 * sc), PAL_NAVY, no);
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        // A / Enter: the highlighted default (wait); B / Esc: also wait; Yes only by clicking it or Y / Space
        if ((hy && click) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false))
            g_prep_skip = 1, g_prep_skip_ask = false;
        else if ((hn && click) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            g_prep_skip_ask = false;
    }
}

// ------------------------------------------------------------------ the data folder (runtime/paths.h): another one
// picked (Settings > Prelaunch, the Game Files window), the data moved there or a fresh start, then a restart there
enum { DF_NONE, DF_ASK, DF_MOVING, DF_FAILED };
static int g_df;                  // (main thread)
static std::string g_df_pick;     // the folder picked (g_lock: the dialog calls back on any thread)
static RtDataTarget g_df_t;
static bool g_df_onedrive;        // the folder picked is synced by OneDrive too (Windows)
static std::atomic<int> g_df_step{0}, g_df_result{0};  // result: 0 running, 1 moved, -1 failed
static std::atomic<float> g_df_frac{0};
static char g_df_err[600];
static bool g_df_auto_move, g_df_auto_fresh;  // (test: LARECOMP_LAUNCHER_DATA_PICK=<folder>:move / :fresh)
static bool g_df_prev;  // the pick is the data folder used before (a new version in another folder): offered at start

static void SDLCALL picked_data(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_df_pick = files[0];
    pthread_mutex_unlock(&g_lock);
}
// Save data (runtime/save_backup.h): backups, restore, import. A save folder picked to import comes from the dialog's thread.
static std::string g_sb_pick, g_sb_confirm;  // (g_sb_pick: g_lock; g_sb_confirm: the backup a restore waits for a yes on)
static void SDLCALL picked_saves(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_sb_pick = files[0];
    pthread_mutex_unlock(&g_lock);
}

static void choose_data_folder(void) { SDL_ShowOpenFolderDialog(picked_data, NULL, g_launcher_win, NULL, false); }
static void df_progress(int step, double f) { g_df_step = step, g_df_frac = (float)f; }
static void *df_move_thread(void *) {
    g_df_result = (g_df_prev ? rt_data_pull : rt_data_move)(g_df_t.path, df_progress, g_df_err, sizeof g_df_err) ? 1 : -1;
    return NULL;
}
static void df_restart(void) {
    unsetenv("LARECOMP_LAUNCHER_DATA_PICK");
    rt_paths_restart();
    snprintf(g_df_err, sizeof g_df_err, "Couldn't restart SkywardRecomp. Start it again to use the new folder.");
    g_df = DF_FAILED;
}

// The question / the move's progress, in front of everything (called at the start screen's top level)
static void data_folder_popup(float s) {
    float sc = s * 720.0f / 1080.0f;
    pthread_mutex_lock(&g_lock);
    std::string pick;
    pick.swap(g_df_pick);
    bool busy = g_busy;
    pthread_mutex_unlock(&g_lock);
    if (!pick.empty() && g_df == DF_NONE && !busy) {
        rt_data_target(pick.c_str(), &g_df_t);
        g_df_onedrive = !g_df_t.refuse[0] && plat_onedrive_synced(g_df_t.path);
        g_df = DF_ASK;
        ImGui::OpenPopup("##datafolder");
    }
    if (!ImGui::BeginPopupModal("##datafolder", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    const RtDataTarget &t = g_df_t;
    const ImVec4 red(1, 0.5f, 0.4f, 1);
    ImVec2 bw(240 * sc, 0);
    ImGui::PushFont(g_font_small);
    ImGui::PushFont(g_font_menu);
    ImGui::TextColored(PAL_ACCENT_V4, g_df == DF_MOVING ? "Moving your data" : "Data folder");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(900 * sc);
    bool close = false, esc = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    auto use_it = [&] {
        stop_background_prep();
        settings_save();
        if (rt_data_use(t.path, g_df_err, sizeof g_df_err)) df_restart();
        else g_df = DF_FAILED;
    };
    if (g_df == DF_ASK) {
        ImGui::TextDisabled("%s", t.path);
        if (g_df_onedrive) ImGui::TextColored(PAL_WARN_V4, "This folder is synced by OneDrive too.");
        ImGui::Spacing();
        if (t.refuse[0]) {
            ImGui::TextColored(red, "%s", t.refuse);
            ImGui::Spacing();
            close = ImGui::Button("OK", bw) || esc;
        } else if (t.has_data && g_df_prev) {
            ImGui::Text("Use existing data in this folder (saves, game files, build cache)?");
            ImGui::TextColored(PAL_WARN_V4, "Use keeps your data in %s. Don't delete that folder.", t.path);
            ImGui::Spacing();
            if (ImGui::Button("Use existing data", bw)) use_it();
            ImGui::SameLine();
            if (ImGui::Button("Move here", bw)) {
                stop_background_prep();  // (no settings_save: it would write into the target; the old folder's settings win)
                g_df = DF_MOVING, g_df_step = 0, g_df_frac = 0, g_df_result = 0;
                pthread_t th;
                if (pthread_create(&th, NULL, df_move_thread, NULL) == 0) pthread_detach(th);
                else snprintf(g_df_err, sizeof g_df_err, "Couldn't start the move."), g_df = DF_FAILED;
            }
            ImGui::SameLine();
            ImGui::SetItemDefaultFocus();
            close = ImGui::Button("Start fresh", bw) || esc;
        } else if (t.has_data) {
            ImGui::Text("This folder already has SkywardRecomp data. Use it?");
            ImGui::Spacing();
            if (ImGui::Button("Use it", bw) || g_df_auto_fresh) g_df_auto_fresh = false, use_it();
            ImGui::SameLine();
            close = ImGui::Button("Cancel", bw) || esc;
        } else {
            bool room = t.free >= t.need;
            ImGui::Text("Move your saves, seeds, settings and game files (%.1f GB) there, or start fresh?", t.bytes / 1e9);
            if (!room) ImGui::TextColored(red, "Not enough free space there to move them (%.1f GB free).", t.free / 1e9);
            ImGui::Spacing();
            ImGui::BeginDisabled(!room);
            if ((ImGui::Button("Move", bw) || g_df_auto_move) && room) {
                g_df_auto_move = false;
                stop_background_prep();
                settings_save();
                g_df = DF_MOVING, g_df_step = 0, g_df_frac = 0, g_df_result = 0;
                pthread_t th;
                if (pthread_create(&th, NULL, df_move_thread, NULL) == 0) pthread_detach(th);
                else snprintf(g_df_err, sizeof g_df_err, "Couldn't start the move."), g_df = DF_FAILED;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Start fresh", bw) || g_df_auto_fresh) g_df_auto_fresh = false, use_it();
            ImGui::SameLine();
            ImGui::SetItemDefaultFocus();
            close = ImGui::Button("Cancel", bw) || esc;
        }
    } else if (g_df == DF_MOVING) {
        static const char *const steps[] = {"Copying...", "Checking the copy...", "Removing the old copy..."};
        int r = g_df_result, k = g_df_step;
        ImGui::Text("%s", r == 1 ? "Restarting..." : steps[k < 0 ? 0 : k > 2 ? 2 : k]);
        ImGui::ProgressBar(r == 1 ? 1.0f : g_df_frac.load(), ImVec2(900 * sc, 0));
        ImGui::TextDisabled("%s", t.path);
        if (r == 1) df_restart();
        else if (r == -1) g_df = DF_FAILED;
    } else {
        ImGui::TextColored(red, "%s", g_df_err);
        ImGui::Text("Your data is still in %s", g_df_prev ? t.path : rt_data_root());
        ImGui::Spacing();
        close = ImGui::Button("OK", bw) || esc;
    }
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (close) g_df = DF_NONE, g_df_prev = false, ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static bool g_files_deleted;  // set here: the start screen goes back to setting the game files up
static void prelaunch_tab(float sc) {
    const ImVec4 gold(0.93f, 0.76f, 0.33f, 1);
    auto heading = [&](const char *t) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(gold, "%s", t);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
    };
    heading("Game files");
    static double gb = -1;
    if (gb < 0) gb = rt_game_files_size(g_root.c_str()) / 1e9;
#ifdef _WIN32
    if (ImGui::Button("Show in Explorer")) reveal_folder(g_root);
#elif defined(__linux__)
    if (ImGui::Button("Open folder")) reveal_folder(g_root);
#else
    if (ImGui::Button("Show in Finder")) reveal_folder(g_root);
#endif
    ImGui::SameLine();
    static bool shot_del = getenv("LARECOMP_LAUNCHER_SHOT") && strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":askdelete");
    if (ImGui::Button("Delete game files") || shot_del) ImGui::OpenPopup("##delfiles"), shot_del = false;  // (test: :askdelete)
    if (ImGui::BeginPopupModal("##delfiles", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(PAL_WARN_V4, "Are you sure?");
        ImGui::PopFont();
        ImGui::Text("This deletes SkywardRecomp's copy of the game files (%.1f GB).", gb);
        ImGui::TextDisabled("Saves, seeds and settings are kept. Your own game files aren't touched.");
        if (rt_dev_mode()) {  // (a source checkout: the files are the project's own extracted/)
            bool abs = !g_root.empty() && (g_root[0] == '/' || (g_root.size() > 1 && g_root[1] == ':'));
            std::string where = abs ? g_root : std::string(rt_data_root()) + "/" + g_root;
            ImGui::TextColored(PAL_ERR_V4, "Dev checkout: this is %s", where.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(220 * sc, 0))) {
            stop_background_prep();
            bool ok = rt_remove_game_files(g_root.c_str());  // (runtime/paths.c: only what the extraction made)
            game_files_changed();
            set_job(ok ? "Game files deleted." : "Could not delete the game files (not in SkywardRecomp's data folder?).", !ok);
            g_files_deleted = true, gb = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Cancel", ImVec2(220 * sc, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    heading("Data folder");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", rt_data_root());
    ImGui::PopTextWrapPos();
    const char *fixed = rt_data_root_fixed();
    ImGui::BeginDisabled(fixed != NULL);
    if (ImGui::Button("Change...")) choose_data_folder();
    ImGui::EndDisabled();
    if (fixed) ImGui::SameLine(), ImGui::TextDisabled("%s", fixed);
    ImGui::Spacing();
    ImGui::Spacing();
    heading("Game font");
    import_fonts();
    bool fs = have_font(FONT_STD, 4), fl = have_font(FONT_LANG, 8), fe = have_font(FONT_EXT, 2);
    ImGui::Text("Text: %s    Chinese / Korean: %s    Button icons: %s",
                fs ? "Nintendo font (imported)" : "substitute (this OS's, per language)",
                fl ? "Nintendo font (imported)" : "substitute", fe ? "imported" : "substitute");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Fonts aren't included. For the original look, import your own copies of "
                        "nintendo_udsg-r_std_003.bfttf (text), the same-named files for Chinese and Korean "
                        "(nintendo_udsg-r_org_zh-cn_003, nintendo_udsg-r_ext_zh-cn_003, nintendo_udjxh-db_zh-tw_003, "
                        "nintendo_udsg-r_ko_003) and nintendo_ext_003.bfttf (button icons). "
                        ".ttf versions with the same names work too.");
    ImGui::PopTextWrapPos();
    if (!g_font_msg.empty()) ImGui::TextWrapped("%s", g_font_msg.c_str());
    static const SDL_DialogFileFilter font_f[] = {{"Switch fonts", "bfttf;ttf"}};
    if (ImGui::Button("Import fonts")) SDL_ShowOpenFileDialog(picked_fonts, NULL, g_launcher_win, font_f, 1, NULL, true);
    if (fs || fl || fe) {
        ImGui::SameLine();
        if (ImGui::Button("Use substitute font")) remove_fonts();
    }
}

// SKY-32 status checklist rows. These read the same checks as the rest of the
// launcher; when SKY-31 lands, swap these for the shared LauncherState value.
enum class StatusCheckLevel { Ok, Warning, Error, Neutral };  // Neutral: not known, says nothing about "ready"

struct StatusCheckRow {
    StatusCheckLevel level;
    std::string detail;
};

static StatusCheckRow status_check_game_files(const std::string &status)
{
    if (status == "Wrong version of the game")
        return { StatusCheckLevel::Ok, "Ready" };  // the version row owns this
    if (status.rfind("Game files incomplete", 0) == 0)
        return { StatusCheckLevel::Error, "Incomplete, copy again" };
    if (status == "Game code not built yet")
        return { StatusCheckLevel::Error, "Set up to build" };
    if (!status.empty())
        return { StatusCheckLevel::Error, "Not set up" };
    if (files_changed(g_root))
        return { StatusCheckLevel::Warning, "Damaged or changed" };
    return { StatusCheckLevel::Ok, "Ready" };
}

static StatusCheckRow status_check_version(const std::string &status)
{
    if (status == "Wrong version of the game")
        return { StatusCheckLevel::Error, "Wrong version" };
    static std::string version_label;  // cached: reading the release is not free per frame
    static int label_of = -1;
    if (label_of != g_files_changes.load()) {
        label_of = g_files_changes.load();
        char label[64];
        const std::string exefs = g_root.empty() ? std::string() : g_root + "/exefs";
        const char *name = game_release_label(exefs.c_str(), label, sizeof(label));
        version_label = name ? name : "";
    }
    if (version_label.empty())
        return { StatusCheckLevel::Warning, "Unknown" };
    return { StatusCheckLevel::Ok, version_label };
}

static StatusCheckRow status_check_update()
{
    pthread_mutex_lock(&g_update_lock);
    const int state = g_update_state;
    const bool available = !g_update_tag.empty();
    pthread_mutex_unlock(&g_update_lock);
    if (state == 1)
        return { StatusCheckLevel::Ok, "Checking" };
    if (state == 3)
        return { StatusCheckLevel::Neutral, "Check failed" };  // no update known: not a reason to hedge the headline
    if (state == 2 && available)
        return { StatusCheckLevel::Warning, "Update available" };
    if (state == 2)
        return { StatusCheckLevel::Ok, "Up to date" };
    return { StatusCheckLevel::Neutral, "Not checked" };  // state 0: never started or disabled
}

static StatusCheckRow status_check_configuration()
{
    if (g_root.empty())
        return { StatusCheckLevel::Error, "Not set" };
    if (g_found.empty())
        return { StatusCheckLevel::Warning, "Not detected" };
    return { StatusCheckLevel::Ok, "Ready" };
}

static ImVec4 status_check_color(StatusCheckLevel level)
{
    if (level == StatusCheckLevel::Ok)
        return PAL_OK_V4;
    if (level == StatusCheckLevel::Warning)
        return PAL_WARN_V4;
    if (level == StatusCheckLevel::Neutral)
        return PAL_DIM_V4;
    return PAL_ERR_V4;
}

// Bottom-anchored at `bottom` (window y) so the headline, 4 rows and the hint never run off the screen.
static void draw_status_checklist(const std::string &status, bool busy, float x0, float bottom, float wrap_w)
{
    StatusCheckRow rows[4] = {
        status_check_game_files(status),
        status_check_version(status),
        status_check_update(),
        status_check_configuration(),
    };
    if (busy)
        rows[0] = { StatusCheckLevel::Warning, "Setting up..." };
    static const char *labels[4] = { "Game files", "Version", "Update", "Configuration" };

    const bool blocked = !busy && (rows[0].level == StatusCheckLevel::Error ||
                                   rows[1].level == StatusCheckLevel::Error ||
                                   rows[3].level == StatusCheckLevel::Error);
    const bool changed = !busy && !blocked && rows[0].level == StatusCheckLevel::Warning;
    const bool optional_update = rows[2].level == StatusCheckLevel::Warning;
    const char *headline = busy ? "Setting up the game files..."
                         : blocked ? "Setup required"
                         : changed ? "Playable, check the game files"
                         : optional_update ? "Playable, update optional" : "Ready to Play";
    const StatusCheckLevel head_level = blocked ? StatusCheckLevel::Error
                                      : (busy || changed || optional_update) ? StatusCheckLevel::Warning
                                                                              : StatusCheckLevel::Ok;
    const char *hint = busy ? "Set up the game files to play."
                     : !status.empty() ? status_hint(status)
                     : changed ? CHANGED_HINT : nullptr;

    const float lh = ImGui::GetTextLineHeightWithSpacing();
    float h = 5 * lh;
    if (hint)
        h += ImGui::CalcTextSize(hint, nullptr, false, wrap_w).y + 2;
    ImGui::SetCursorPos(ImVec2(x0, bottom - h));
    ImGui::TextColored(status_check_color(head_level), "%s", headline);

    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorPosX(x0);
        const StatusCheckRow &row = rows[i];
        if (row.level == StatusCheckLevel::Ok) {
            // The Switch font has no checkmark glyph; draw one with lines.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float u = ImGui::GetTextLineHeight();
            const ImU32 green = PAL_OK;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + u * 0.55f),
                                                ImVec2(p.x + u * 0.22f, p.y + u * 0.82f), green, 2);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + u * 0.22f, p.y + u * 0.82f),
                                                ImVec2(p.x + u * 0.68f, p.y + u * 0.18f), green, 2);
            ImGui::SetCursorPosX(x0 + u * 1.25f);
        } else {
            ImGui::TextColored(status_check_color(row.level), "%s",
                               row.level == StatusCheckLevel::Error ? "x"
                               : row.level == StatusCheckLevel::Warning ? "!" : "-");
            ImGui::SameLine();
        }
        ImGui::TextColored(PAL_TEXT2_V4, "%s", labels[i]);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", row.detail.c_str());
    }
    if (hint) {
        ImGui::SetCursorPosX(x0);
        ImGui::PushTextWrapPos(x0 + wrap_w);
        ImGui::TextDisabled("%s", hint);
        ImGui::PopTextWrapPos();
    }
}

extern "C" void launcher_run(const char *root) {
    g_root = root;
    g_start_screen = 1;
    static bool update_checked;  // (once per launch, not again after a "Title screen" reboot)
    if (!update_checked && !getenv("LARECOMP_NO_UPDATE_CHECK")) {
        update_checked = true;
        start_update_check();
    }
    ui_window_init();
    SDL_Window *win = ui_window();
    g_launcher_win = win;
#ifdef __APPLE__
    CAMetalLayer *layer = (__bridge CAMetalLayer *)ui_metal_layer();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)ui_metal_device();
    id<MTLCommandQueue> queue = [dev newCommandQueue];
#else
    GpuDevice dev = NULL;
#endif

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();  // the in-game menu's, when back here after a reboot
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;
    // fonts belong to this context: back here via "Title screen" (in-place reboot) the previous run's are gone with
    // its context, so load them again (a stale pointer here aborted in PushFont - user, 0.1 RC)
    g_font_small = g_font_menu = g_font_title = NULL;
    // Baloo 2 SemiBold (OFL, shipped: third_party/fonts/Baloo2) - rounded, game-like; it runs small, hence the sizes
    char baloo[1024];
    std::string own = rt_resource("Baloo2-SemiBold.ttf", "third_party/fonts/Baloo2/Baloo2-SemiBold.ttf", baloo, sizeof baloo) ? baloo : "";
    const char *fonts[] = {getenv("LARECOMP_LAUNCHER_FONT") ? getenv("LARECOMP_LAUNCHER_FONT") : "",  // (test)
                           own.c_str(), "fonts/nintendo_udsgr_std_003.ttf", "/System/Library/Fonts/Avenir Next.ttc",
                           "/System/Library/Fonts/Helvetica.ttc", "C:/Windows/Fonts/segoeui.ttf"};
    static const ImWchar ranges[] = {0x20, 0xFF, 0x2039, 0x203A, 0x2192, 0x2192, 0x2713, 0x2713, 0x25B8, 0x25B8, 0};  // + ‹ › → ✓ ▸
    for (const char *p : fonts) {
        if (g_font_menu || !*p || !file_exists(p)) continue;
        bool b2 = p == own.c_str() || getenv("LARECOMP_LAUNCHER_FONT");
        g_font_small = io.Fonts->AddFontFromFileTTF(p, 30 * (b2 ? 1.75f : 1), NULL, ranges);
        g_font_menu = io.Fonts->AddFontFromFileTTF(p, 52 * (b2 ? 1.85f : 1), NULL, ranges);
        // the title font (the biggest by far: ~210 px) draws only "Skyward Sword HD": a few glyphs, not the whole range
        static const ImWchar title_ranges[] = {0x20, 0x20, 0x30, 0x39, 0x41, 0x5A, 0x61, 0x7A, 0};
        g_font_title = io.Fonts->AddFontFromFileTTF(p, 92 * (b2 ? 2.3f : 1), NULL, title_ranges);
    }
    if (!g_font_menu) g_font_small = g_font_menu = g_font_title = io.Fonts->AddFontDefault();
    ImGuiStyle &st = ImGui::GetStyle();
    pal_apply_style(st);
    st.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.14f);
    st.Colors[ImGuiCol_NavCursor] = PAL_ACCENT_V4;
    st.FrameRounding = st.WindowRounding = 10;
#ifdef __APPLE__
    ImGui_ImplSDL3_InitForMetal(win);
    ImGui_ImplMetal_Init(dev);
#else
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
#endif
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);  // (controllers: launcher_pad_event)
    launcher_pad_open_all();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    // an older version's game data image: the game reads the romfs/ folder itself now - 5.8 GB back. Only when that
    // folder is there: without it romfs.bin IS the game files (a .nsp setup, served whole by runtime/services/fs.c).
    if (check_copied_files(g_root).empty() && file_exists(g_root + "/romfs.bin") && file_exists(g_root + "/romfs") &&
        rt_remove_game_file(g_root.c_str(), "romfs.bin"))
        fprintf(stderr, "[launcher] removed the old game data image (romfs.bin)\n");
    std::string status = check_game_files(g_root);
    load_art(dev);
    // the title movie's still is only drawn when there is no artwork (and the bundled art is always there): don't hold
    // a second full-size texture for nothing
    if (!g_art && check_copied_files(g_root).empty()) load_background(dev);  // (also behind an update of the game code)
    std::vector<std::string> seeds = list_seeds();
    std::vector<std::string> choices = {"Vanilla"};
    choices.insert(choices.end(), seeds.begin(), seeds.end());
    int choice = 0;
    for (size_t i = 0; i < seeds.size(); i++)
        if (seeds[i] == g_settings.rando_seed) choice = (int)i + 1;
    Page page = MAIN;  // Home first for everyone; "Set Up Game" is the way into SETUP
    bool play = false;
    // LARECOMP_LAUNCHER_SHOT=<frame>[:<page>] (test): save build/launcher.png at that frame and exit; page: settings[:<tab>] / askdelete / setup / extracting / preparing
    long shot_at = getenv("LARECOMP_LAUNCHER_SHOT") ? atol(getenv("LARECOMP_LAUNCHER_SHOT")) : 0, frame = 0;
    if (shot_at) {
#ifdef __APPLE__
        layer.framebufferOnly = NO;
#endif
        const char *pg = strchr(getenv("LARECOMP_LAUNCHER_SHOT"), ':');
        if (pg && (!strncmp(pg + 1, "settings", 8) || !strcmp(pg + 1, "askdelete"))) page = SETTINGS, menu_tabs_focus(true);
        if (pg && !strcmp(pg + 1, "setup")) page = SETUP;
        if (pg && !strcmp(pg + 1, "preparing")) page = SETUP, g_prep_frac = 0.42f, set_job("Preparing shaders");
        if (pg && !strcmp(pg + 1, "skipask")) page = SETUP, g_prep_frac = 0.42f, set_job("Preparing shaders"), g_prep_skip_ask = true;
        if (pg && !strncmp(pg + 1, "fail", 4)) page = SETUP, set_failure(atoi(pg + 5));  // (the failure messages)
        if (pg && !strncmp(pg + 1, "settings", 8) && pg[9] == ':') g_tab_request_ext = atoi(pg + 10);  // settings:<tab>
        if (pg && !strcmp(pg + 1, "delete")) page = SETUP, g_confirm_delete = true;
        if (pg && !strcmp(pg + 1, "tip")) g_settings.menu_tip_seen = false, g_tip_test = true;
        if (pg && !strcmp(pg + 1, "aspect")) g_settings.aspect_asked = g_settings.menu_tip_seen = false, g_tip_test = true;
    }

    static int runs;  // (test: =once - only the first time, so a "Title screen" reboot lands on the start screen)
    const char *ap = getenv("LARECOMP_LAUNCHER_AUTOPLAY");
    if (ap && status.empty() && !(runs && !strcmp(ap, "once"))) play = true;  // (test: PLAY at once)
    if (getenv("LARECOMP_LAUNCHER_PREPARE") && status.empty() && !runs) {  // (test) the setup's shader preparation
        g_busy = true;                                                      // alone, then PLAY with AUTOPLAY
        page = SETUP, play = false;
        pthread_t th;
        pthread_create(&th, NULL, [](void *) -> void * {
            prepare_shaders();
            set_job("Game files ready.");
            pthread_mutex_lock(&g_lock);
            g_busy = false;
            pthread_mutex_unlock(&g_lock);
            return NULL;
        }, NULL);
        pthread_detach(th);
    }
    runs++;
    if (const char *t = getenv("LARECOMP_LAUNCHER_DATA_PICK")) {  // (test) <folder>[:move / :fresh]: picked as the data folder
        std::string p = t;
        size_t c = p.rfind(':');
        if (c != std::string::npos && (p.substr(c) == ":move" || p.substr(c) == ":fresh"))
            g_df_auto_move = p.substr(c) == ":move", g_df_auto_fresh = p.substr(c) == ":fresh", p.resize(c);
        g_df_pick = p;
    } else if (char prev[1100]; rt_previous_data_root(prev, sizeof prev)) {
        fprintf(stderr, "[launcher] data from before in %s: offered\n", prev);
        g_df_pick = prev, g_df_prev = true;
    }
    if (const char *t = getenv("LARECOMP_LAUNCHER_EXTRACT")) {  // (test) <folder>: set the game files up, log, exit
        g_src = t;
        g_busy = true;
        g_exit_after_setup = true;  // (the setup runs on a thread: the window draws its progress meanwhile, not a black frame)
        page = SETUP;
        pthread_t th;
        pthread_create(&th, NULL, extract_thread, NULL);
        pthread_detach(th);
    }
    if (const char *t = getenv("LARECOMP_LAUNCHER_SETUP")) {  // (test) <folder>: Set up started on the start screen
        g_src = t;
        g_busy = true;
        page = SETUP;
        pthread_t th;
        pthread_create(&th, NULL, extract_thread, NULL);
        pthread_detach(th);
    }
    // An update: the game files are there and only the game code is out of date - rebuilt at once, by itself (only
    // the pieces that changed: runtime/gamebuild.c), with the progress bar; then the start screen as usual. A failure
    // stays on the setup page with what went wrong.
    if (!g_busy && !status.empty() && check_copied_files(g_root).empty() && gamebuild_available() &&
        !getenv("LARECOMP_LAUNCHER_DELETE")) {
        fprintf(stderr, "[launcher] game code out of date: updating it\n");
        page = SETUP;
        start_game_code_update();
    }
    if (getenv("LARECOMP_LAUNCHER_DELETE")) {  // (test) what DELETE GAME FILES does, logged; then the start screen
        double gb = rt_game_files_size(g_root.c_str()) / 1e9;
        stop_background_prep();
        bool ok = rt_remove_game_files(g_root.c_str());
        game_files_changed();
        status = check_game_files(g_root);
        fprintf(stderr, "[launcher] delete %.1f GB: %s -> %s\n", gb, ok ? "ok" : "refused", status.c_str());
        if (!status.empty()) { g_bg = 0; page = SETUP; }
    }
    if (g_onedrive < 0) {  // (once per launch: a "Title screen" reboot keeps it)
        const char *t = getenv("LARECOMP_TEST_ONEDRIVE");
        g_onedrive = t ? atoi(t) != 0 : plat_onedrive_synced(rt_data_root());
        if (g_onedrive) fprintf(stderr, "[launcher] the data folder is synced by OneDrive%s: %s\n",
                                g_settings.onedrive_kept ? " (kept there)" : "", rt_data_root());
    }
    // the shader preparation of every later start: in the background (first setups and updates do theirs above)
    if (status.empty() && !g_busy && !getenv("LARECOMP_LAUNCHER_PREPARE")) start_background_prep();
    // (test) LARECOMP_LAUNCHER_PLAY_AT=<s>: PLAY that many seconds in; LARECOMP_LAUNCHER_SKIP_AT=<s>: Skip then
    const char *play_at_env = getenv("LARECOMP_LAUNCHER_PLAY_AT"), *skip_at_env = getenv("LARECOMP_LAUNCHER_SKIP_AT");
    uint64_t loop_t0 = SDL_GetTicks(), last_frame = loop_t0, bg_gap = 0, bg_frames = 0;
    bool bg_logged = false;
    for (;;) {
        uint64_t now = SDL_GetTicks();
        if (g_bg_prep) bg_frames++, bg_gap = std::max(bg_gap, now - last_frame);
        else if (!bg_logged && bg_frames) {  // (how the start screen kept up meanwhile)
            bg_logged = true;
            fprintf(stderr, "[launcher] background shader preparation: %llu frames meanwhile, longest %llu ms\n",
                    (unsigned long long)bg_frames, (unsigned long long)bg_gap);
#ifndef __APPLE__
    vk_antilag_pause(true);
#endif
        }
        last_frame = now;
        if (play_at_env && status.empty() && now - loop_t0 >= (uint64_t)(atof(play_at_env) * 1000)) play = true, play_at_env = NULL;
        if (skip_at_env && g_prep_wait && now - loop_t0 >= (uint64_t)(atof(skip_at_env) * 1000)) g_prep_skip = 1, skip_at_env = NULL;
        // PLAY while that still runs: its bar (with Skip) until it's done
        if (play && g_bg_prep) {
            play = false, g_prep_wait = true;
            fprintf(stderr, "[launcher] PLAY: waiting for the shader preparation\n");
        }
        if (g_prep_wait && !g_bg_prep) {
            g_prep_wait = false, play = true;
            fprintf(stderr, "[launcher] shader preparation %s: playing\n", g_prep_skip ? "skipped" : "done");
        }
        if (play) break;
        menu_apply_window_requests(win);  // (the Video tab's Fullscreen)
        // the seed PLAY would start, kept current (Settings > Gameplay greys out what that seed forces)
        snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice ? choices[choice].c_str() : "");
        if (g_files_deleted) {  // (Settings > Prelaunch deleted them)
            g_files_deleted = false;
            status = check_game_files(g_root);
            if (!status.empty()) { g_bg = 0; bind_capture_cancel(); settings_save(); page = SETUP; }
        }
        if (g_play_seed_req) {  // the Randomizer tab's Play: that seed, then PLAY as if pressed (first-launch questions)
            g_play_seed_req = false;
            seeds = list_seeds();
            choices = {"Vanilla"};
            choices.insert(choices.end(), seeds.begin(), seeds.end());
            choice = 0;
            for (size_t i = 0; i < seeds.size(); i++)
                if (seeds[i] == g_play_seed) choice = (int)i + 1;
            bind_capture_cancel();
            settings_save();
            page = MAIN;
            g_play_pending = true;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_cursor_event(&e);  // (pad / key: the cursor hides; ImGui's backend leaves it so, below)
            if (page == SETTINGS && bind_capture_event(&e)) continue;  // (waiting for a new binding: controls_ui.mm)
            if (!launcher_pad_event(e)) ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) exit(0);
        }
        // Idle: nothing moves unless an event, a held key / button, a text cursor or a background job asks for it, so
        // draw ~10 fps (~4 fps unfocused; none minimized / hidden) and wake at once on any event. The start screen
        // has no animation of its own, so the lower rate shows nothing different. (tests keep their frame counts)
        static uint64_t last_active;
        bool minimized = SDL_GetWindowFlags(win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED);
        {
            pthread_mutex_lock(&g_lock);
            bool job = g_busy;
            pthread_mutex_unlock(&g_lock);
            bool hot = job || g_bg_prep || g_prep_wait || shot_at || play_at_env || skip_at_env || io.WantTextInput ||
                       now - last_active < 500;
            for (int k = ImGuiKey_NamedKey_BEGIN; !hot && k < ImGuiKey_NamedKey_END; k++) hot = io.KeysData[k - ImGuiKey_NamedKey_BEGIN].Down;
            bool focused = SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS;
            if (minimized || (!focused && !job && !g_bg_prep && !g_prep_wait && !shot_at)) SDL_WaitEventTimeout(NULL, 250);
            else if (!hot) SDL_WaitEventTimeout(NULL, 100);
        }
        // extraction finished: re-check, load the background
        pthread_mutex_lock(&g_lock);
            last_active = SDL_GetTicks();
        bool busy = g_busy;
        pthread_mutex_unlock(&g_lock);
        static bool was_busy;
        if (was_busy && !busy) {
            g_build_cache_changed = true;
            status = check_game_files(g_root);
            if (g_exit_after_setup) {
                fprintf(stderr, "[launcher] setup: %s -> %s\n", g_job.c_str(), status.empty() ? "READY" : "NOT READY");
                exit(0);
            }
            if (status.empty()) { if (!g_bg && !g_art) load_background(dev); page = MAIN; }
            if (status.empty() && ap && getenv("LARECOMP_LAUNCHER_PREPARE")) play = true;
            g_auto_update = false;
        }
        was_busy = busy;

#ifdef __APPLE__
        int w, h;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        layer.drawableSize = CGSizeMake(w, h);
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (minimized) continue;  // (no GPU work while nobody can see the window; events above still wake us)
            if (!drawable) { SDL_Delay(16); continue; }
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.06, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            ImGui_ImplMetal_NewFrame(rp);
#else
        {
            // one frame per refresh of the window's display: the swapchain presents without waiting (mailbox, for the
            // game's own pacing: device.c), so the start screen alone would draw as fast as the GPU goes
            static uint64_t next_frame;
            const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            uint64_t period = (uint64_t)(1e9 / (dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60.0f)), now = plat_now_ns();
            bool on_time = next_frame > now && next_frame - now <= period;  // (else late, or the rate changed: start over)
            if (on_time) plat_sleep_until_ns(next_frame);
            next_frame = (on_time ? next_frame : now) + period;
            static const float clear_col[4] = {0.04f, 0.05f, 0.06f, 1};
            if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
            ImGui_ImplVulkan_NewFrame();
#endif
            if (input_cursor_hidden()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            ImGui_ImplSDL3_NewFrame();
            // the backend (manual mode, no gamepads of its own) clears HasGamepad every frame, and ImGui then drops the
            // controller presses launcher_pad_event fed in and turns gamepad navigation off: set it again while a
            // controller is connected
            if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            ImGui::NewFrame();

            ImVec2 ds = io.DisplaySize;
            float s = ds.y / 720.0f;  // layout scale (fonts are built for ~2x)
            ImGui::GetIO().FontGlobalScale = s * 0.5f;
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            // an update by itself: only its bar, on the start screen's background (gray without it; a failure or
            // Cancel then shows the Game Files page)
            const bool update_only = g_auto_update && busy;
            const bool art_on = g_art ? true : false;  // (the artwork on every page: not an empty gradient)
            if (art_on || (g_bg && (status.empty() || update_only))) {  // cover the window, keep the aspect
                float ia = art_on ? (float)g_art_w / g_art_h : (float)g_bg_w / g_bg_h, wa = ds.x / ds.y;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (wa > ia) { float v = ia / wa; uv0.y = (1 - v) / 2; uv1.y = 1 - uv0.y; }
                else { float u = wa / ia; uv0.x = (1 - u) / 2; uv1.x = 1 - uv0.x; }
                bg->AddImage(art_on ? ART_TEXID : BG_TEXID, ImVec2(0, 0), ds, uv0, uv1);
            } else if (update_only) {
                bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(10, 30, 70, 255));
            } else {
                bg->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(18, 50, 110, 255), IM_COL32(50, 110, 190, 255),
                                            IM_COL32(10, 28, 64, 255), IM_COL32(6, 16, 40, 255));
            }
            if (!update_only) {  // a shadow banner fading in from the left behind the menu, and one from the right behind the version
                const float stops[][2] = {{0.0f, 150}, {0.3f, 130}, {0.44f, 90}, {0.55f, 50}, {0.64f, 18}, {0.74f, 0}};
                for (int i = 0; i + 1 < (int)(sizeof stops / sizeof stops[0]); i++) {
                    ImU32 a = IM_COL32(6, 10, 34, (int)stops[i][1]), b = IM_COL32(6, 10, 34, (int)stops[i + 1][1]);
                    bg->AddRectFilledMultiColor(ImVec2(ds.x * stops[i][0], 0), ImVec2(ds.x * stops[i + 1][0], ds.y), a, b, b, a);
                }
                float y0 = ds.y - 190 * s, y1 = ds.y - 135 * s, y2 = ds.y - 40 * s, y3 = ds.y, x0r = ds.x * 0.5f;
                ImU32 c0 = IM_COL32(6, 10, 34, 0), c1 = IM_COL32(6, 10, 34, 235);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y0), ImVec2(ds.x, y1), c0, c0, c1, c0);  // (feathered top)
                bg->AddRectFilledMultiColor(ImVec2(x0r, y1), ImVec2(ds.x, y2), c0, c1, c1, c0);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y2), ImVec2(ds.x, y3), c0, c1, c0, c0);  // (feathered bottom)
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ds);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::Begin("##launcher", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            const bool waiting = g_prep_wait;  // (the menu waits under the bar)
            ImGui::BeginDisabled(waiting);
            ImGui::SetScrollX(0);  // (the start screen never scrolls; a long seed name or a stray nav move mustn't shift it)
            ImGui::SetScrollY(0);
            float x0 = 90 * s;
            // typing in a text field / the on-screen keyboard (osk.h): B / Esc are for it, not "back" (from the frame
            // before too, so the Esc that ends the typing doesn't also leave the page)
            static bool typed_before;
            bool typing = typed_before || ui_text_busy();
            ImGui::SetCursorPos(ImVec2(x0, 285 * s));  // (as before: the other pages start from here)

            if (page == MAIN) {
                // One action per state. Title, version and status are read at a glance; details stay below.
                const bool ready = home_ready(status);
                const char *action = home_action_label(status);
                bool hov = false;

                // a translucent navy card behind title, status, button and links: the artwork stays visible around it
                // (drawn first through channel 0, once the content's height is known)
                ImDrawList *card_dl = ImGui::GetWindowDrawList();
                card_dl->ChannelsSplit(2);
                card_dl->ChannelsSetCurrent(1);
                const float card_top = 70 * s;
                ImGui::SetCursorPos(ImVec2(x0, 120 * s));
                ImGui::PushFont(g_font_title);
                const char *title = "Skyward Sword HD";
                ImVec2 tp = ImGui::GetCursorScreenPos();
                text_fx(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), tp,
                        IM_COL32(255, 255, 255, 255), IM_COL32(110, 190, 255, 255), title, 3 * s);
                ImGui::Dummy(ImGui::CalcTextSize(title));
                ImGui::PopFont();

                const std::string version = home_game_version();
                ImGui::PushFont(g_font_small);
                ImGui::SetCursorPosX(x0);
                if (!version.empty()) {  // no game files: nothing to name
                    ImGui::TextDisabled("Version %s", version.c_str());
                    ImGui::SetCursorPos(ImVec2(x0, ImGui::GetCursorPos().y + 8 * s));
                }
                draw_home_chip(home_status_chip(status, busy), ready && !busy, s);
                ImGui::PopFont();

                ImGui::SetCursorPosY(ImGui::GetCursorPos().y + 26 * s);
                static bool focused_once;
                if (!focused_once) { ImGui::SetKeyboardFocusHere(); focused_once = true; }
                ImGui::SetCursorPosX(x0);
                bool pressed = draw_home_primary(action, s, &hov, !busy);

                int arrow = 0;
                if (ready) {
                    ImGui::SetCursorPos(ImVec2(x0, ImGui::GetCursorPos().y + 8 * s));
                    ImGui::PushFont(g_font_menu);
                    float bw = std::max(360 * s, ImGui::CalcTextSize(action).x + 60 * s);
                    ImGui::PopFont();
                    arrow = draw_home_seed(s, x0 + bw * 0.5f, choices, choice);
                }
                if (pressed && arrow && ImGui::IsMouseReleased(0)) {  // a mouse click on an arrow: cycle
                    choice = (choice + arrow + (int)choices.size()) % (int)choices.size();
                    pressed = false;
                }
                if (g_play_pending && ready) pressed = true, g_play_pending = false;
                // first PLAY ever: the aspect ratio question, then how to reach our menu, then the game
                if (pressed && ready) {
                    if (!g_settings.aspect_asked) ImGui::OpenPopup("##aspect");
                    else if (!g_settings.menu_tip_seen) ImGui::OpenPopup("##menutip");
                    else play = true;
                } else if (pressed) {
                    page = SETUP;  // "Set Up Game": the same files page, now the primary path into it
                }
                if (g_tip_test) ImGui::OpenPopup(g_settings.aspect_asked ? "##menutip" : "##aspect"), g_tip_test = false;
                if (aspect_prompt(s)) {
                    if (g_settings.menu_tip_seen) play = true;
                    else ImGui::OpenPopup("##menutip");
                }
                if (menu_tip(s)) play = true;
                if (hov && ready) {
                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft))
                        choice = (choice + (int)choices.size() - 1) % (int)choices.size();
                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))
                        choice = (choice + 1) % (int)choices.size();
                    // left / right are ours while the action is selected: ImGui's navigation would otherwise
                    // take them too, and scroll the window (the button loses its highlight, the cycling stops)
                    ImGuiID own = ImGui::GetID("##play_lr");
                    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight})
                        ImGui::SetKeyOwner(k, own);
                }

                // Secondary navigation: one small row, still keyboard and gamepad reachable. Each item starts where
                // the one before ends (measured), so a long update label can never run into QUIT.
                float links_y = ImGui::GetCursorPos().y + 28 * s, links_x = x0;
                auto link = [&](const char *label, bool enabled) {
                    ImGui::SetCursorPos(ImVec2(links_x, links_y));
                    bool hit = menu_item(label, enabled);
                    ImGui::PushFont(g_font_small);  // (menu_item's width: the text plus 40 px)
                    links_x += ImGui::CalcTextSize(label).x + 40 + 24 * s;
                    ImGui::PopFont();
                    return hit;
                };
                g_menu_gap = 0.35f;
                g_menu_small = true;
                if (g_refocus == SETTINGS) { ImGui::SetKeyboardFocusHere(), g_refocus = MAIN; }
                if (link("SETTINGS", true)) {
                    static bool opened;
                    menu_tabs_focus(!opened);  // first time this session: Prelaunch; later: the selected tab
                    opened = true;
                    page = SETTINGS;
                }
                if (ready) {
                    if (g_refocus == SETUP) { ImGui::SetKeyboardFocusHere(), g_refocus = MAIN; }
                    if (link("GAME FILES", !busy)) page = SETUP;
                }
                {  // a newer release opens its page; otherwise the row asks GitHub again
                    pthread_mutex_lock(&g_update_lock);
                    std::string upd = g_update_tag;
                    int us = g_update_state;
                    pthread_mutex_unlock(&g_update_lock);
                    std::string ul = !upd.empty() ? "UPDATE TO " + tag_label(upd, false)
                                   : us == 1 ? "CHECKING FOR UPDATES..." : us == 2 ? "UP TO DATE" : us == 3 ? "UPDATE CHECK FAILED"
                                   : "CHECK FOR UPDATES";
                    if (link(ul.c_str(), upd.empty() ? us != 1 : true)) {
                        if (!upd.empty()) SDL_OpenURL((RELEASE_PAGE + upd).c_str());
                        else start_update_check();
                    }
                }
                if (link("QUIT", true)) exit(0);
                g_menu_small = false;
                {
                    ImVec2 wp0 = ImGui::GetWindowPos();
                    float pad = 40 * s, cw = std::max(links_x - x0, 720 * s);
                    ImVec2 ca(wp0.x + x0 - pad, wp0.y + card_top);
                    ImVec2 cb(wp0.x + std::min(x0 + cw + pad, ds.x - 20 * s), wp0.y + links_y + 70 * s);
                    card_dl->ChannelsSetCurrent(0);
                    card_dl->AddRectFilled(ca, cb, IM_COL32(8, 22, 52, 205), 22 * s);
                    card_dl->AddRect(ca, cb, PAL_ACCENT_A(70), 22 * s, 0, 1.5f * s);
                    card_dl->ChannelsMerge();
                }
            } else if (page == SETTINGS) {
                // the in-game menu's tabs (runtime/ui/menu.mm), in a window like the in-game one; tabs that need a
                // loaded game say so
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;  // (all the height: a slim margin)
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, PAL_PANEL_V4);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##settings", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(PAL_ACCENT_V4, "Settings");
                ImGui::PopFont();
                bool back;
                {  // close: a drawn X in the corner (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? PAL_ACCENT : PAL_TEXT;
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);  // (before the pages run, like the menu)
                menu_tabs_draw(sc, g_font_small, g_font_menu, false, prelaunch_tab);
                int cap_dev;
                bind_capture_active(&cap_dev, NULL, NULL);
                if (!popup && !typing && cap_dev < 0 && !controls_ui_took_back() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back) {
                    bind_capture_cancel();
                    settings_save();
                    page = MAIN, g_refocus = SETTINGS;
                    std::string cur = choices[choice];  // seeds made / renamed / deleted in the Randomizer tab
                    seeds = list_seeds();
                    choices = {"Vanilla"};
                    choices.insert(choices.end(), seeds.begin(), seeds.end());
                    choice = 0;
                    for (size_t i = 0; i < choices.size(); i++)
                        if (choices[i] == cur) choice = (int)i;
                }
            } else if (update_only) {
                pthread_mutex_lock(&g_lock);
                std::string job = g_job;
                pthread_mutex_unlock(&g_lock);
                draw_busy_overlay(ds, job, false);
            } else {
                // SETUP: the game files, in a window like Settings (option rows: runtime/ui/option_rows.h) - choose the
                // folder with the game files, set up; once set up, delete. Each row's explanation on the right.
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, PAL_PANEL_V4);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##setup", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(PAL_ACCENT_V4, "Game Files");
                ImGui::PopFont();
                bool back = false;
                if (!busy) {  // close: the drawn X in the corner, as Settings (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? PAL_ACCENT : PAL_TEXT;
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                ImGui::Spacing();

                pthread_mutex_lock(&g_lock);
                std::string src = g_src, job = g_job, job_hint = g_job_hint, found = g_found, job_tech = g_job_tech, upick = g_update_pick, kpick = g_keys_pick;
                bool failed = g_job_failed;
                int job_kind = g_job_kind;
                bool scan_base = g_scan_base, scan_update = g_scan_update, scan_keys = g_scan_keys, scan_nsp = g_scan_nsp;
                static bool prev_busy;
                if (busy != prev_busy) g_copied_ok = -1;  // (a setup just ran or ended)
                prev_busy = busy;
                int copied_ok = g_copied_ok;
                pthread_mutex_unlock(&g_lock);
                if (copied_ok < 0) {  // once per pick / setup, not every frame
                    copied_ok = check_copied_files(g_root).empty();
                    pthread_mutex_lock(&g_lock);
                    g_copied_ok = copied_ok;
                    pthread_mutex_unlock(&g_lock);
                }
                bool ready = status.empty() && !busy;  // (set up: the window offers to delete them)
                static double files_gb = -1;
                // chosen: "Selected" with a green check (long paths ran over the label); the path is in the
                // explanation, and choosing again opens the picker again
                std::string src_v = src.empty() ? "Choose..." : "Selected";
                auto check_left_of_value = [&](bool chosen, const std::string &v) {
                    if (!chosen) return;
                    ImVec2 rb = ImGui::GetItemRectMax(), ra = ImGui::GetItemRectMin();
                    float fs = ImGui::GetFontSize(), vw = ImGui::CalcTextSize(v.c_str()).x;
                    float x = rb.x - 16 * sc - vw - fs * 0.95f, cy = (ra.y + rb.y) * 0.5f, u = fs * 0.62f;
                    ImU32 g = PAL_OK;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddLine(ImVec2(x, cy), ImVec2(x + u * 0.35f, cy + u * 0.35f), g, fs * 0.11f);
                    dl->AddLine(ImVec2(x + u * 0.35f, cy + u * 0.35f), ImVec2(x + u, cy - u * 0.4f), g, fs * 0.11f);
                };
                // two sentences at most; the version is under Advanced details, the folder picture under "Show example"
                const std::string src_d = "Choose the folder with your own copy of the game, or its .nsp package. "
                                          "Then set it up once: it copies and compiles what SkywardRecomp needs.";
                const std::string &job_line = job;

                // the controller's cursor starts on the first row (not the X), a few frames while the window lays out
                static int seen_frame = -10, focus_first;  // (not drawn last frame: the window has just opened)
                if (seen_frame != ImGui::GetFrameCount() - 1) focus_first = 4;
                seen_frame = ImGui::GetFrameCount();
                auto first_row = [&] {
                    if (focus_first > 0 && focus_first--) ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                };
                opt_begin("##files", sc);
                if (!ready) {
                    opt_section("Your game");
                    if (busy) opt_disable_next();
                    static const SDL_DialogFileFilter nsp_f[] = {{"Switch package", "nsp"}, {"All files", "*"}};
                    static const SDL_DialogFileFilter keys_f[] = {{"Switch keys", "keys"}, {"All files", "*"}};
                    // One picker for the whole set: the scan fills the checklist below and we ask only for what it
                    // could not find.
                    if (opt_action("Choose Game Files", src_v.c_str(), src_d.c_str(), true))
                        SDL_ShowOpenFolderDialog(picked, NULL, win, NULL, false);
                    check_left_of_value(!src.empty(), src_v);
                    first_row();

                    auto base_name = [](const std::string &path) {
                        size_t i = path.find_last_of("/\\");
                        return i == std::string::npos ? path : path.substr(i + 1);
                    };
                    const bool is_nsp = picked_nsp_package();  // (SKY-34's rule: only a .nsp needs prod.keys, its update is optional)
                    const std::string key_file = detected_key_file();
                    const bool have_files = scan_base || copied_ok;
                    const bool have_update = scan_update || !upick.empty();
                    if (!src.empty()) {  // the scan's checklist: one line each, the result coloured
                        auto check = [&](const char *name, const char *val, bool good, bool bad) {
                            ImGui::TextColored(PAL_DIM_V4, "%s", name);
                            ImGui::SameLine(ImGui::GetFontSize() * 7);
                            ImGui::TextColored(good ? PAL_OK_V4 : bad ? PAL_WARN_V4 : PAL_DIM_V4, "%s", val);
                        };
                        check("Game files", have_files ? "Found" : "Missing", have_files, !have_files);
                        check("Update 1.0.1", have_update ? "Found" : "Not chosen (optional)", have_update, false);
                        if (is_nsp) check("prod.keys", key_file.empty() ? "Missing" : "Found", !key_file.empty(), key_file.empty());
                        if (!have_files)
                            ImGui::TextDisabled("Choose a folder with exefs/ and romfs/, or a .nsp.");
                    }
                    if (!src.empty() && have_files && update_field_applies()) {  // optional: never blocks Set up
                        std::string upd_v = upick.empty() ? "Choose..." : base_name(upick);
                        if (busy) opt_disable_next();
                        if (opt_action("Choose update (optional)", upd_v.c_str(), "Optional. Adds version 1.0.1 over the base game; 1.0.0 works too.", true))
                            SDL_ShowOpenFileDialog(picked_update, NULL, win, nsp_f, 2, NULL, false);
                        check_left_of_value(!upick.empty(), upd_v);
                    }
                    if (!src.empty() && is_nsp) {  // (stays visible once found, to show and change it)
                        std::string keys_v = key_file.empty() ? "Choose..." : "Key file detected";
                        if (busy) opt_disable_next();
                        if (opt_action("Choose prod.keys", keys_v.c_str(), "Only needed for a .nsp package: your prod.keys, if it is not next to the .nsp.", true))
                            SDL_ShowOpenFileDialog(picked_keys, NULL, win, keys_f, 2, NULL, false);
                        check_left_of_value(!key_file.empty(), keys_v);
                    }
                    // ONE big primary button; it looks enabled as soon as there is something to set up
                    {
                        const bool can = !busy && !(src.empty() && !copied_ok);
                        const bool resume = !busy && src.empty() && resumable_build();
                        const char *bl = busy ? "Working..." : resume ? "Resume Game Code" : "Set Up Game";
                        ImGui::Dummy(ImVec2(0, 8 * sc));
                        ImGui::PushFont(g_font_menu);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * sc);
                        ImGui::PushStyleColor(ImGuiCol_Button, can ? ImGui::ColorConvertU32ToFloat4(PAL_SKY) : ImGui::ColorConvertU32ToFloat4(PAL_SKY_OFF));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(can ? PAL_SKY_HI : PAL_SKY_OFF));
                        ImGui::PushStyleColor(ImGuiCol_Border, PAL_ACCENT_V4);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, can ? 2 * sc : 0);
                        if (ImGui::Button(bl, ImVec2(-1, 78 * sc)) && can) {
                            pthread_mutex_lock(&g_lock);
                            g_busy = true;
                            pthread_mutex_unlock(&g_lock);
                            pthread_t t;
                            pthread_create(&t, NULL, extract_thread, NULL);
                            pthread_detach(t);
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(3);
                        ImGui::PopFont();
                    }
                    files_gb = -1;  // (re-measured after a setup)
                } else {
                    // DELETE: frees SkywardRecomp's copy (runtime/paths.c removes only what the setup made, only inside
                    // the data root). Saves, seeds, settings and the user's own files stay.
                    if (files_gb < 0) files_gb = rt_game_files_size(g_root.c_str()) / 1e9;
                    char gb[32];
                    snprintf(gb, sizeof gb, "%.1f GB", files_gb);
                    opt_section("Game files");
                    if (!g_confirm_delete) {
                        if (opt_action("Delete game files", gb, "Frees the space SkywardRecomp's copy of the game files takes. "
                                       "Saves, seeds and settings are kept, and your own game files aren't touched: you can "
                                       "set the game files up again from them.", true))
                            g_confirm_delete = true;
                        first_row();
                    } else {
                        if (opt_action("Yes, delete them", gb, "Deletes the extracted game files now.", true)) {
                            stop_background_prep();
                            bool ok = rt_remove_game_files(g_root.c_str());
                            game_files_changed();
                            status = check_game_files(g_root);
                            if (ok) set_job("Game files deleted.");
                            else set_job("Couldn't delete the game files", true, "They aren't in SkywardRecomp's data folder, "
                                         "so they were left alone.");
                            if (!status.empty()) g_bg = 0;
                            g_confirm_delete = false, files_gb = -1;
                        }
                        if (opt_action("Cancel", "", "Keeps the game files.", true)) g_confirm_delete = false;
                    }
                }
                ImGui::Dummy(ImVec2(0, 6 * sc));
                if (ImGui::CollapsingHeader("More")) {  // storage, build cache and saves: not part of the first run
                {  // where all of it is kept: another folder (a bigger drive)
                    const char *fixed = rt_data_root_fixed();
                    std::string d = std::string("Where SkywardRecomp keeps game files, saves, seeds and settings.\n\n") +
                                    rt_data_root() + (fixed ? std::string("\n\n") + fixed : "");
                    opt_section("Storage");
                    if (busy || fixed) opt_disable_next();
                    if (opt_action("Data folder", "Change...", d.c_str(), true)) choose_data_folder();
                }
                bool code_rebuild = check_copied_files(g_root).empty() && gamebuild_available();
                if (!busy && (has_build_cache() || code_rebuild || status.empty())) {
                    opt_section("Build cache");
                    if (code_rebuild && opt_action("Rebuild game code", "", "Builds the game code again from the game files. "
                                                   "Pieces that didn't change are reused.", true)) {
                        char why[512] = "";
                        g_build_cache_changed = true;
                        if (build_cache_rebuild(g_root.c_str(), why, sizeof why)) start_game_code_update();
                        else set_job("Could not rebuild the game code.", true, why);
                    }
                    if (status.empty()) {
                        if (g_bg_prep) opt_disable_next();  // (its growth would be saved again by this process)
                        if (opt_action("Rebuild shader cache", g_bg_prep ? "Preparing..." : "", "Removes the saved shader "
                                       "cache and restarts SkywardRecomp to prepare the shaders again.", true)) {
                            stop_background_prep();
                            settings_save();
                            shader_cache_freeze();  // (Windows: this process waits out the restarted one, its saver alive)
                            for (const char *f : {"cache/vk_pipelines.bin", "cache/vk_pipelines.bin.tag",
                                                  "cache/shaders.metalarchive", "cache/shaders.metalarchive.tag"})
                                remove(f);  // (relative to the data folder: the working directory)
                            plat_remove_tree("cache/spirv");
                            fprintf(stderr, "[launcher] shader cache removed: restarting\n");
                            rt_paths_restart();
                            set_job("Shader cache removed.", true, "Start SkywardRecomp again to prepare the shaders.");
                        }
                    }
                    if (has_build_cache() && opt_action("Clear build cache", "", "Removes the saved game-code build files. "
                                                        "The next setup compiles everything again.", true)) {
                        char why[512] = "";
                        g_build_cache_changed = true;
                        if (build_cache_clear(g_root.c_str(), why, sizeof why)) set_job("Build cache cleared.");
                        else set_job("Could not clear the build cache.", true, why);
                    }
                }
                if (!busy) {
                    opt_section("Save data");
                    char why[512] = "", made[64] = "";
                    std::string picked_dir;
                    pthread_mutex_lock(&g_lock);
                    picked_dir.swap(g_sb_pick);
                    pthread_mutex_unlock(&g_lock);
                    if (!picked_dir.empty()) {
                        if (save_backup_import(rt_data_root(), picked_dir.c_str(), why, sizeof why)) set_job("Save data imported.");
                        else set_job("Could not import the save data.", true, why);
                    }
                    if (opt_action("Back up now", "", "Copies your saves (and each randomizer seed's) to the backups "
                                   "folder. The newest 20 are kept.", true)) {
                        if (save_backup_create(rt_data_root(), made, sizeof made, why, sizeof why)) set_job("Saves backed up.");
                        else set_job("Could not back up the saves.", true, why);
                    }
                    if (opt_action("Import save...", "", "Takes saves from a folder: a backup folder, or one with a save "
                                   "file (savedata0.bin, or zelda.sav from an emulator or a Switch). Your current saves are "
                                   "backed up first.", true))
                        SDL_ShowOpenFolderDialog(picked_saves, NULL, g_launcher_win, NULL, false);
                    static SaveBackup list[SAVE_BACKUP_KEEP];
                    int nb = save_backup_list(rt_data_root(), list, SAVE_BACKUP_KEEP);
                    for (int i = 0; i < nb; i++) {
                        char when[48], size[32];
                        time_t t = (time_t)list[i].when;
                        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&t));
                        snprintf(size, sizeof size, "%.1f MB", list[i].bytes / 1e6);
                        std::string label = std::string("Restore ") + when + (strstr(list[i].name, "-auto") ? " (auto)" : "");
                        bool asking = g_sb_confirm == list[i].name;
                        if (opt_action(asking ? "Yes, restore it" : label.c_str(), size, asking ? "Replaces your current saves "
                                       "with this backup. They are backed up first." : "Puts the saves of this backup back. "
                                       "Your current saves are backed up first.", true)) {
                            if (!asking) g_sb_confirm = list[i].name;
                            else {
                                g_sb_confirm.clear();
                                if (save_backup_restore(rt_data_root(), list[i].name, why, sizeof why)) set_job("Saves restored.");
                                else set_job("Could not restore the saves.", true, why);
                                break;
                            }
                        }
                    }
                    if (!g_sb_confirm.empty() && opt_action("Cancel", "", "Keeps your current saves.", true)) g_sb_confirm.clear();
                    if (opt_action("Open backups folder", "", "Shows the folder the backups are kept in.", true)) {
                        std::string dir = std::string(rt_data_root()) + "/backups";
                        mkdir(dir.c_str(), 0755);
                        SDL_OpenURL(("file:///" + dir).c_str());
                    }
                }
                }  // More
                if (!job_line.empty()) {  // the current step / result
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(failed ? PAL_ERR_V4 : busy ? PAL_ACCENT_V4
                                              : PAL_OK_V4, "%s", job_line.c_str());
                    if (failed && !job_hint.empty())  // (what to do next, in the rows' own text colour)
                        ImGui::TextColored(PAL_TEXT2_V4, "%s", job_hint.c_str());
                    if (failed && job_kind && !busy) {  // a setup failure: one primary action, the raw reason collapsed
                        ImGui::Dummy(ImVec2(0, 6 * sc));
                        // (the same picker as the Game files folder row; no new modal)
                        if (ImGui::Button("Choose Another Folder")) SDL_ShowOpenFolderDialog(picked, NULL, win, NULL, false);
                        if (!job_tech.empty()) {
                            ImGui::Dummy(ImVec2(0, 4 * sc));
                            if (ImGui::CollapsingHeader("Technical details"))  // (collapsed by default)
                                ImGui::TextColored(PAL_DIM_V4, "%s", job_tech.c_str());
                        }
                    }
                    ImGui::PopTextWrapPos();
                } else if (!busy && !status.empty() && status != "No game files yet") {
                    // (no setup run yet this session: why the game files on disk can't be played - an incomplete copy
                    // opens this window instead of crashing on Play)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(PAL_ERR_V4, "%s", status.c_str());
                    ImGui::TextColored(PAL_TEXT2_V4, "%s", status_hint(status));
                    ImGui::PopTextWrapPos();
                } else if (ready && files_changed(g_root)) {  // (playable, but a damaged or changed file: #391)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(PAL_WARN_V4, "%s", CHANGED_HEAD);
                    ImGui::TextColored(PAL_TEXT2_V4, "%s", CHANGED_HINT);
                    ImGui::PopTextWrapPos();
                }
                if (!ready && g_onedrive > 0 && !g_settings.onedrive_kept) {  // (before the copy is the time to move)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    int r = onedrive_notice(1.5f * sc, ImGui::GetContentRegionAvail().x, false);
                    if (r == 1) onedrive_keep();
                    else if (r == 2) choose_data_folder();
                }
                if (ready) opt_end();
                else {  // before the setup: under the description, what the folder to choose looks like, and the hint
                    opt_split();
                    opt_desc_text();
                    float fs = ImGui::GetFontSize(), ew = fminf(ImGui::GetContentRegionAvail().x, fs * 15);
                    ImGui::Dummy(ImVec2(0, fs * 0.5f));  // (a paragraph's space)
                    if (ImGui::CollapsingHeader("Show example")) {  // (collapsed: the folder picture only when asked for)
                        draw_folder_example(ew);
                        ImGui::Dummy(ImVec2(0, fs * 0.3f));  // (its shadow)
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ew);
                        ImGui::TextColored(PAL_DIM_V4, "%s", PICK_FOLDER_HINT);
                        ImGui::PopTextWrapPos();
                    }
                    opt_split_end();
                }
                // SKY-37: Level 3 diagnostics live here, never in the main flow (both states).
                ImGui::Dummy(ImVec2(0, 10 * sc));
                if (ImGui::CollapsingHeader("Advanced details")) {
                    static std::string ver_label;  // (game_release_label reads files: only when the folder changes)
                    static int ver_of = -1;
                    static std::string ver_root;
                    if (ver_of != g_files_changes.load() || ver_root != g_root) {
                        ver_of = g_files_changes.load(), ver_root = g_root;
                        char rel[64] = {0};
                        const char *v = game_release_label((g_root + "/exefs").c_str(), rel, sizeof rel);
                        ver_label = v ? v : "";
                    }
                    const ImVec4 dim_txt(0.55f, 0.58f, 0.52f, 1);
                    const char *dash = "-";
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(dim_txt, "Game path\n%s", src.empty() ? dash : src.c_str());
                    ImGui::TextColored(dim_txt, "Version\n%s", ver_label.empty() ? dash : ver_label.c_str());
                    ImGui::TextColored(dim_txt, "ExeFS\n%s", g_root.empty() ? dash : (g_root + "/exefs").c_str());
                    ImGui::TextColored(dim_txt, "RomFS\n%s", g_root.empty() ? dash : (g_root + "/romfs").c_str());
                    if (!found.empty())
                        ImGui::TextColored(dim_txt, "Detection\n%s", found.c_str());
                    ImGui::TextColored(dim_txt, "Update\n%s", upick.empty() ? "None" : upick.c_str());
                    ImGui::TextColored(dim_txt, "Validation\n%s", status.empty() ? "OK" : status.c_str());
                    if (!kpick.empty())
                        ImGui::TextColored(dim_txt, "Keys\n%s", kpick.c_str());
                    ImGui::TextColored(dim_txt, "Config\n%s/skywardrecomp.ini", rt_data_root());
                    ImGui::TextColored(dim_txt, "Log\n%s/skywardrecomp.log", rt_data_root());
                    ImGui::PopTextWrapPos();
                    if (ImGui::SmallButton("Open data folder")) SDL_OpenURL((std::string("file:///") + rt_data_root()).c_str());
                }
                if (g_confirm_delete && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    g_confirm_delete = false;  // (B / Esc: back out of the question first)
                else if (!busy && !typing && g_df == DF_NONE && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                                                 ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back && !busy) {
                    settings_save();
                    g_confirm_delete = false;
                    page = MAIN, g_refocus = SETUP;
                }
                static bool test_busy = getenv("LARECOMP_LAUNCHER_SHOT") && (strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":extracting") ||
                                                                         strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":preparing") ||
                                                                         strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":skipask"));
                if (busy || test_busy) draw_busy_overlay(ds, job, test_busy);  // (everything dimmed, a bar in front)

            }

            // status (lower left) and version (lower right) - not under the Settings / Game Files windows
            if (page == MAIN) {
            ImGui::PushFont(g_font_small);
            draw_status_checklist(status, busy, x0, ds.y - 70 * s, fminf(ds.x * 0.45f, 560 * s));
            if (g_onedrive > 0 && !g_settings.onedrive_kept) {
                float w = fminf(ds.x * 0.42f, 560 * s);
                ImGui::SetCursorPos(ImVec2(ds.x - w - 60 * s, 60 * s));
                int r = onedrive_notice(s, w, status.empty());
                if (r == 1) onedrive_keep();
                else if (r == 2) choose_data_folder();
            }
            if (const char *f = getenv("LARECOMP_LAUNCHER_FONT")) {  // (test) which typeface this window shows
                const char *b = strrchr(f, '/');
                ImGui::GetWindowDrawList()->AddText(ImVec2(ImGui::GetWindowPos().x + 20 * s, ImGui::GetWindowPos().y + 16 * s),
                                                    IM_COL32(255, 255, 255, 255), b ? b + 1 : f);
            }
            {  // version, and a newer release when the update check found one (lower right). The background is
               // always the title movie's still: bright clouds here, under a shadow banner from the right
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImFont *font = ImGui::GetFont();
                float fs = ImGui::GetFontSize(), right = ImGui::GetWindowPos().x + ds.x - 60 * s;
                auto text = [&](ImVec2 p, float size, ImU32 col, const char *t, bool bold) {
                    for (float d : {2.5f, 1.5f, 0.8f}) dl->AddText(font, size, ImVec2(p.x + d * s, p.y + d * s), IM_COL32(4, 8, 30, 150), t);
                    dl->AddText(font, size, p, col, t);
                    if (bold) dl->AddText(font, size, ImVec2(p.x + 0.7f * s, p.y), col, t);
                };
                std::string ver = tag_label(LARECOMP_VERSION, true);
                float vs = fs * 1.4f, y = ImGui::GetWindowPos().y + ds.y - 134 * s;
                ImVec2 vsz = font->CalcTextSizeA(vs, FLT_MAX, 0, ver.c_str());
                text(ImVec2(right - vsz.x, y), vs, IM_COL32(255, 255, 255, 255), ver.c_str(), false);
                pthread_mutex_lock(&g_update_lock);
                std::string upd = g_update_tag;
                pthread_mutex_unlock(&g_update_lock);
                if (!upd.empty()) {
                    const char *arrow = font->FindGlyphNoFallback(0x2192) ? " \xe2\x86\x92" : " \xe2\x96\xb8";
                    std::string pre = "Update available!  ", link = "DOWNLOAD " + tag_label(upd, false) + arrow;
                    float wp = font->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
                    ImVec2 lsz = font->CalcTextSizeA(fs, FLT_MAX, 0, link.c_str());
                    ImVec2 lp(right - lsz.x, y + vsz.y + 2 * s);
                    text(ImVec2(lp.x - wp, lp.y), fs, IM_COL32(200, 214, 250, 255), pre.c_str(), false);
                    ImGui::SetCursorScreenPos(lp);
                    bool click = ImGui::InvisibleButton("update", lsz);
                    bool hover = ImGui::IsItemHovered();
                    // the menu's hover colours (yellow to orange), lighter while the link itself is hovered
                    ImU32 col = hover ? IM_COL32(220, 245, 255, 255) : PAL_ACCENT;
                    ImU32 col_b = hover ? IM_COL32(150, 205, 255, 255) : IM_COL32(90, 170, 245, 255);
                    text_fx(dl, font, fs, lp, col, col_b, link.c_str(), 1.5f * s);
                    text_fx(dl, font, fs, ImVec2(lp.x + 0.7f * s, lp.y), col, col_b, link.c_str(), 0);  // (faux bold)
                    if (hover) {
                        dl->AddLine(ImVec2(lp.x, lp.y + lsz.y), ImVec2(lp.x + lsz.x, lp.y + lsz.y), col, 1.5f * s);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    if (click) SDL_OpenURL((RELEASE_PAGE + upd).c_str());
                }
            }
            ImGui::PopFont();
            }
            ImGui::EndDisabled();
            data_folder_popup(s);  // (another data folder picked: move / start fresh, then restart)
            ImGui::End();
            ImGui::PopStyleColor();
            if (waiting) draw_busy_overlay(ds, "", false, true);
            osk_draw(g_font_menu, s * 0.7f);  // (a text field activated with the controller)
            typed_before = ui_text_busy();

            ImGui::Render();
#ifdef __APPLE__
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            id<MTLTexture> shot = nil;
            if (shot_at && ++frame == shot_at) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:drawable.texture.pixelFormat
                                             width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
                td.storageMode = MTLStorageModeShared;
                shot = [dev newTextureWithDescriptor:td];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                [bl copyFromTexture:drawable.texture toTexture:shot];
                [bl endEncoding];
            }
            [cb presentDrawable:drawable];
            [cb commit];
            if (shot) {
                [cb waitUntilCompleted];
                save_png(shot, getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png");
                exit(0);
            }
#else
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
            bool shot = shot_at && ++frame == shot_at;
            vk_frame_end(shot ? getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png" : NULL);
            if (shot) exit(0);
#endif
        }
    }
    // play: remember the choice, hand the window to the game
    snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice ? choices[choice].c_str() : "");
    settings_save();
#ifndef __APPLE__
    vk_antilag_pause(false);
#endif
#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    vk_imgui_backend_shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
    g_bg = 0;
    g_start_screen = 0;
}
