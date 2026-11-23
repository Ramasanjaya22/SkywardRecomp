// Finds the user's game in a folder they picked: the extracted files (exefs + romfs folders) or a base-game .nsp package.
// Plain C, no runtime dependencies, so tools/test_game_find.c builds it on its own.
//
// What it does that the first version (which took the first .nsp the directory listing gave) didn't:
//   - every .nsp is opened and checked (a PFS0 container; the title id in its ticket's name, readable without keys), so an
//     update, a DLC, another game, a damaged or non-NSP file is skipped instead of being picked,
//   - the whole folder (bounded) is looked at, not just up to the first .nsp, so unrelated files can't hide the right one,
//   - several fits are tie-broken the same way every time (shallowest, then name), not by directory order,
//   - it says what it picked and why each other candidate was skipped (game_find_describe).
#pragma once
#include <stddef.h>

enum { GAME_FIND_NOTES = 24, GAME_FIND_PATH = 1024 };

typedef struct {
    char path[GAME_FIND_PATH];
    char why[200];
} GameFindNote;

typedef struct {
    char exefs[GAME_FIND_PATH], romfs[GAME_FIND_PATH];  // the extracted game files ("" if not found); both set = usable as is
    char nsp[GAME_FIND_PATH];                           // the base-game package to extract ("" if none)
    char nsp_why[200];                                  // why that one was chosen
    char update[GAME_FIND_PATH];                        // the 1.0.1 update package found with it ("" if none): applied over the base game
    int unusable;                                       // .xci / .nca / .nsz / .xcz seen (not supported)
    int dirs, files, nsp_seen;                          // what was looked at
    int budget_hit;                                     // stopped early: the folder is huge, pick a more specific one
    GameFindNote notes[GAME_FIND_NOTES];                // the candidates that were skipped, and why
    int nnotes, nmore;                                  // nmore: notes beyond the table
} GameFind;

// What a .nsp is, best first: GAME_NSP_BASE (its ticket names the base game), GAME_NSP_NAMED (no ticket, but the file name carries the
// base game's title id, as dumps are named "Game [01002DA013484000][v0].nsp"), GAME_NSP_UNKNOWN (a valid package without a ticket or a
// title in its name: its game can't be told without keys, so it's the fallback) or one of the refusals (why is filled in).
enum { GAME_NSP_BASE = 1, GAME_NSP_NAMED = 2, GAME_NSP_UNKNOWN = 3, GAME_NSP_BAD = -1, GAME_NSP_UPDATE = -2 /* the 1.0.1 update: only usable together with a base game package, see GameFind.update */, GAME_NSP_OTHER = -3 };
int game_nsp_classify(const char *path, char *why, size_t why_size);

// check (may be NULL): says whether an exefs folder is a supported build of the game (0 = no, why filled in). Folders it refuses
// are skipped, so another game's dump in the same folder doesn't hide the right .nsp.
typedef int (*GameFindExefsCheck)(const char *exefs_dir, char *why, size_t why_size);
void game_find(const char *dir, GameFindExefsCheck check, GameFind *g);

// A short paragraph for the user: what was chosen, and what was skipped and why (also what an empty folder had in it).
void game_find_describe(const GameFind *g, char *out, size_t out_size);
