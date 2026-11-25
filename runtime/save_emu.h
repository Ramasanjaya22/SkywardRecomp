#pragma once

#include <stdbool.h>
#include <stddef.h>

/* A save found in an emulator's data folder. */
typedef struct {
    char emu[32];      /* "eden", "fritchii", "ryujinx" */
    char display[48];  /* "Eden", "Fritschii", "Ryujinx" */
    char profile[40];  /* profile folder / id */
    char path[1024];   /* folder holding wiiking2.sav */
    long long when;    /* mtime of wiiking2.sav */
} SaveEmuHit;

/* All known emulator saves on this host, newest first. */
int save_emu_find(SaveEmuHit *out, int max);

/* Saves under dir, newest first. emu is a table name or "folder" for any save. */
int save_emu_find_under(const char *dir, const char *emu, SaveEmuHit *out, int max);

/* True if a known emulator is running; puts its display name in display. */
bool save_emu_running(char *display, size_t n);

/* The data folders save_emu_find looked in that exist here, comma separated ("nothing found: where else to look"). */
int save_emu_where(char *out, size_t cap);

/* Copy this PC's save into an emulator's save folder, keeping its old save in
 * <root>/backups/export-<emu>-<stamp>/. False: why says why, nothing changed. */
bool save_emu_export(const char *root, const char *dst, const char *emu,
                     char *backup_out, size_t bn, char *why, size_t cap);
