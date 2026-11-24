// Is the extraction already in extracted/ the one this .nsp (and update) would make? Then the launcher keeps it: the
// extraction rewrites 7+ GB, so it must not run again for the same package. Pure text checks on tools/extract.py's
// extraction-manifest.json and index.json; runtime/ui/launcher.mm reads the files. tools/test_extract_current.py.
#pragma once
#include <stddef.h>
#include <string.h>

// The text after `"key":` (spaces skipped), or NULL.
static inline const char *ec_value(const char *json, const char *key) {
    char pat[64];
    size_t n = strlen(key);
    if (n + 4 > sizeof pat) return NULL;
    pat[0] = '"', memcpy(pat + 1, key, n), pat[n + 1] = '"', pat[n + 2] = ':', pat[n + 3] = 0;
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += n + 3;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;
    return p;
}

static inline int ec_u64(const char *json, const char *key, unsigned long long *out) {
    const char *p = ec_value(json, key);
    if (!p || *p < '0' || *p > '9') return 0;
    unsigned long long v = 0;
    for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (unsigned)(*p - '0');
    *out = v;
    return 1;
}

// 1 if the JSON string value of `key` ends in the file name `name` (folders and their slashes don't matter)
static inline int ec_name_is(const char *json, const char *key, const char *name) {
    const char *p = ec_value(json, key);
    if (!p || *p != '"') return 0;
    const char *end = p + 1, *base = end;
    for (; *end && *end != '"'; end++)
        if (*end == '\\' || *end == '/') {
            if (*end == '\\' && end[1] == '"') { end++; continue; }  // (an escaped quote)
            base = end + 1;
        }
    return *end == '"' && strlen(name) == (size_t)(end - base) && !strncmp(base, name, (size_t)(end - base));
}

// src_name / src_size: the package that was extracted last (the update if there is one, else the base game).
// romfs_bytes: the size of extracted/romfs.bin. An interrupted extraction fails the size compare.
static inline int extraction_current(const char *manifest, const char *index, unsigned long long romfs_bytes,
                                     const char *src_name, unsigned long long src_size, int has_update) {
    unsigned long long v;
    if (!manifest || !index) return 0;
    if (!ec_name_is(manifest, "source", src_name)) return 0;
    if (!ec_u64(manifest, "source_size", &v) || v != src_size) return 0;
    const char *u = ec_value(manifest, "update");
    if (!u || (!strncmp(u, "true", 4)) != (has_update != 0)) return 0;
    if (!ec_u64(index, "romfs_size", &v) || v != romfs_bytes || !v) return 0;
    return 1;
}
