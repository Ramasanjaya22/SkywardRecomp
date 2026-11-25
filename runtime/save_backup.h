// Save data backups: <data root>/backups/save-YYYYMMDD-HHMMSS[-auto]/ holds a copy of save/ and every rando/<seed>/save/
// (the same relative paths). The newest SAVE_BACKUP_KEEP are kept; an unchanged automatic backup is skipped instead of
// duplicated. Nothing here is left half written: a backup is built
// under a temporary name, and a restore/import is staged next to the saves, then swapped in by renames.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define SAVE_BACKUP_KEEP 20

typedef struct { char name[64]; long long when; unsigned long long bytes; } SaveBackup;

// A backup of the saves now; its name in name_out. false: why says why (nothing to back up, a file can't be read...).
bool save_backup_create(const char *root, char *name_out, size_t n, char *why, size_t cap);
// At start: when this app version differs from the one that last ran here (backups/last_version.txt), an "-auto"
// backup of the saves, then the version noted. true: one was made (its name in name_out).
bool save_backup_on_update(const char *root, const char *version, char *name_out, size_t n);
// This session's automatic backup: an "-auto" one when the saves changed since the newest backup that matches them,
// nothing when they didn't (no duplicates), pruned to the newest SAVE_BACKUP_KEEP. true: one was made.
bool save_backup_auto(const char *root, char *name_out, size_t n);
// The backups, newest first. Returns how many (at most max).
int save_backup_list(const char *root, SaveBackup *out, int max);
// The saves replaced by that backup (an "-auto" backup of the current ones first). false: why says why, the saves are
// as they were (a file held open by the game, for one).
bool save_backup_restore(const char *root, const char *name, char *why, size_t cap);
// Saves from src_dir: a backup folder (one with save/ or rando/ in it), or a folder with game saves (wiiking2.sav,
// savedataN.bin, zelda.sav = savedata0.bin; skip.dat, N.screenshot, *.flagsx side files; also in save_N/ folders).
// Save files are size-checked against the live ones; side files may be empty. An "-auto" backup first.
bool save_backup_import(const char *root, const char *src_dir, char *why, size_t cap);
