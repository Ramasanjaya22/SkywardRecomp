#pragma once

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
