// Save data backups (runtime/save_backup.c), driven by tools/test_save_backup.py:
//   test_save_backup <root> create | list | restore <name> | import <dir> | update <version>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"
#include "platform.h"
#include "save_backup.h"
#include "save_emu.h"

// (the platform file's crash handling, never reached here)
void rt_crash_explain(char *out, size_t n, const char *log_path) { (void)log_path; if (n) out[0] = 0; }
bool rt_fault_watch(uint64_t a, uint64_t pc) { (void)a, (void)pc; return false; }
uint64_t g_fault_lr;
void rt_fault_report(const char *what, uint64_t a, uint64_t pc, uint64_t lr) {
    (void)a, (void)pc, (void)lr;
    fprintf(stderr, "fault: %s\n", what);
    abort();
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { (void)pc, (void)offset; return NULL; }

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *root = argv[1], *cmd = argv[2];
    char why[600] = "", name[64] = "";
    if (!strcmp(cmd, "create")) {
        bool ok = save_backup_create(root, name, sizeof name, why, sizeof why);
        printf("CREATE %d %s|%s\n", ok, name, why);
    } else if (!strcmp(cmd, "list")) {
        static SaveBackup b[64];
        int n = save_backup_list(root, b, 64);
        for (int i = 0; i < n; i++) printf("BACKUP %s %lld %llu\n", b[i].name, b[i].when, b[i].bytes);
        printf("COUNT %d\n", n);
    } else if (!strcmp(cmd, "restore") && argc > 3) {
        bool ok = save_backup_restore(root, argv[3], why, sizeof why);
        printf("RESTORE %d|%s\n", ok, why);
    } else if (!strcmp(cmd, "import") && argc > 3) {
        bool ok = save_backup_import(root, argv[3], why, sizeof why);
        printf("IMPORT %d|%s\n", ok, why);
    } else if (!strcmp(cmd, "update") && argc > 3) {
        bool ok = save_backup_on_update(root, argv[3], name, sizeof name);
        printf("UPDATE %d %s|\n", ok, name);
    } else if (!strcmp(cmd, "emu-list")) {
        SaveEmuHit hits[64];
        int n = save_emu_find(hits, 64);
        for (int i = 0; i < n; ++i)
            printf("EMU %s|%s|%s|%s|%lld\n", hits[i].emu, hits[i].profile, hits[i].display, hits[i].path, hits[i].when);
    } else {
        return 2;
    }
    return 0;
}
