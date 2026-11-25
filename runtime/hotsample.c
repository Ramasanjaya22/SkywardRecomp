#include "hotsample.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

ptrdiff_t hot_bin(const uintptr_t *starts, size_t n, uintptr_t pc) {
    if (!n || pc < starts[0]) return -1;
    size_t lo = 0, hi = n;  // starts[lo] <= pc, and pc < starts[hi] when hi < n
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (starts[mid] <= pc) lo = mid; else hi = mid;
    }
    return (ptrdiff_t)lo;
}

typedef struct { uint64_t guest; uint32_t count; } Hot;
static int by_count(const void *a, const void *b) {
    const Hot *x = a, *y = b;
    if (x->count != y->count) return x->count < y->count ? 1 : -1;
    return x->guest < y->guest ? -1 : x->guest > y->guest;
}

long hot_write(const char *path, const uint64_t *guest, const uint32_t *count, const unsigned char *keep, size_t n, size_t limit) {
    Hot *hot = malloc((n + 1) * sizeof *hot);
    if (!hot) return -1;
    size_t m = 0;
    for (size_t i = 0; i < n; i++) if (count[i] && (!keep || keep[i])) hot[m++] = (Hot){guest[i], count[i]};
    qsort(hot, m, sizeof *hot, by_count);
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { free(hot); return -1; }
    size_t written = 0;
    for (; written < m && written < limit; written++) fprintf(f, "0x%llx %u\n", (unsigned long long)hot[written].guest, hot[written].count);
    bool ok = fclose(f) == 0;
    free(hot);
#ifdef _WIN32
    ok = ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
#else
    ok = ok && rename(tmp, path) == 0;
#endif
    if (!ok) { remove(tmp); return -1; }
    return (long)written;
}

#if defined(_WIN32) && !defined(HOTSAMPLE_TEST)
#include "rt.h"

#define MAX_THREADS 256
#define WRITE_EVERY_MS 5000  // the list is rewritten this often: a run ended by TerminateProcess / kill still leaves one
#ifdef _M_ARM64
#define CONTEXT_PC(c) ((uintptr_t)(c).Pc)
#else
#define CONTEXT_PC(c) ((uintptr_t)(c).Rip)
#endif

static HANDLE g_threads[MAX_THREADS];
static volatile LONG g_reserved;  // slots handed out (a slot is filled a moment later: the sampler skips empty ones)
static volatile LONG g_stop;
static HANDLE g_sampler;
static uintptr_t *g_starts;  // host addresses of the generated functions, sorted
static uint64_t *g_guest;    // their guest addresses
static unsigned char *g_keep;  // set for the main code's functions (the list is for the game code, not the runtime)
static uint32_t *g_count;    // samples per function
static size_t g_n;
static uintptr_t g_lo, g_hi;  // the game library's image
static uint64_t g_total, g_other;
static uint64_t g_limit = 400;
static char g_out[1100];

typedef struct { uintptr_t host; uint64_t guest; unsigned char main; } Fn;
static int by_host(const void *a, const void *b) {
    uintptr_t x = ((const Fn *)a)->host, y = ((const Fn *)b)->host;
    return x < y ? -1 : x > y;
}

// Threads are collected whenever LARECOMP_HOTLIST_OUT is set, also those started before the game library is loaded.
void hotsample_thread(uintptr_t handle) {
    const char *out = getenv("LARECOMP_HOTLIST_OUT");
    if (!out || !*out) return;
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)handle, GetCurrentProcess(), &copy, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0)) return;
    LONG slot = InterlockedIncrement(&g_reserved) - 1;
    if (slot < MAX_THREADS) g_threads[slot] = copy; else CloseHandle(copy);
}

static void record(uintptr_t pc) {
    g_total++;
    if (pc < g_lo || pc >= g_hi) { g_other++; return; }
    ptrdiff_t i = hot_bin(g_starts, g_n, pc);
    if (i < 0) g_other++; else g_count[i]++;
}

static void write_list(void) {
    long n = hot_write(g_out, g_guest, g_count, g_keep, g_n, (size_t)g_limit);
    if (n < 0) LOG("[hotsample] can't write %s\n", g_out);
}

// Nothing between SuspendThread and ResumeThread may take a lock or allocate: the suspended thread may hold it.
static DWORD WINAPI sampler(void *arg) {
    (void)arg;
    ULONGLONG next = GetTickCount64() + WRITE_EVERY_MS;
    while (!g_stop) {
        LONG n = g_reserved < MAX_THREADS ? g_reserved : MAX_THREADS;
        for (LONG i = 0; i < n; i++) {
            HANDLE h = g_threads[i];
            if (!h) continue;
            if (SuspendThread(h) == (DWORD)-1) {  // the thread ended: its handle is of no use any more
                g_threads[i] = NULL;
                CloseHandle(h);
                continue;
            }
            CONTEXT cx;
            cx.ContextFlags = CONTEXT_CONTROL;
            uintptr_t pc = GetThreadContext(h, &cx) ? CONTEXT_PC(cx) : 0;
            ResumeThread(h);
            if (pc) record(pc);
        }
        if (GetTickCount64() >= next) { write_list(); next = GetTickCount64() + WRITE_EVERY_MS; }
        Sleep(1);
    }
    return 0;
}

static void hotsample_finish(void) {
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_sampler, 2000);
    write_list();
    LOG("[hotsample] %llu samples, %llu outside the generated code (%.0f%%), list in %s\n",
        (unsigned long long)g_total, (unsigned long long)g_other, g_total ? 100.0 * g_other / g_total : 0.0, g_out);
}

// Gives up: no sampler runs, the collected thread handles are closed.
static void abandon(Fn *fns) {
    free(fns);
    free(g_starts); free(g_guest); free(g_keep); free(g_count);
    g_starts = NULL; g_guest = NULL; g_keep = NULL; g_count = NULL; g_n = 0;
    for (LONG i = 0; i < MAX_THREADS; i++) if (g_threads[i]) { CloseHandle(g_threads[i]); g_threads[i] = NULL; }
}

void hotsample_start(const GameLibTables *t) {
    const char *out = getenv("LARECOMP_HOTLIST_OUT");
    if (!out || !*out || !t || g_sampler) return;
    snprintf(g_out, sizeof g_out, "%s", out);
    const char *limit = getenv("LARECOMP_HOTLIST_N");
    if (limit && atoi(limit) > 0) g_limit = (uint64_t)atoi(limit);
    size_t nm = (size_t)*t->main_count, ns = (size_t)*t->sdk_count;
    Fn *fns = malloc((nm + ns + 1) * sizeof *fns);
    if (!fns) return;
    for (size_t i = 0; i < nm; i++) fns[i] = (Fn){(uintptr_t)t->main_fns[i], t->main_addrs[i], 1};
    for (size_t i = 0; i < ns; i++) fns[nm + i] = (Fn){(uintptr_t)t->sdk_fns[i], t->sdk_addrs[i], 0};
    g_n = nm + ns;
    if (!g_n) { abandon(fns); return; }
    qsort(fns, g_n, sizeof *fns, by_host);
    g_starts = malloc(g_n * sizeof *g_starts);
    g_guest = malloc(g_n * sizeof *g_guest);
    g_keep = malloc(g_n * sizeof *g_keep);
    g_count = calloc(g_n, sizeof *g_count);
    if (!g_starts || !g_guest || !g_keep || !g_count) { abandon(fns); return; }
    for (size_t i = 0; i < g_n; i++) { g_starts[i] = fns[i].host; g_guest[i] = fns[i].guest; g_keep[i] = fns[i].main; }
    free(fns);
    // The game library's image: samples inside it are generated code (or its few helpers: shortcut, those count for the
    // last function), anything else is the runtime, the kernel or a wait.
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)g_starts[0], &mbi, sizeof mbi) || !mbi.AllocationBase) { abandon(NULL); return; }
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)mbi.AllocationBase;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    g_lo = (uintptr_t)mbi.AllocationBase;
    g_hi = g_lo + nt->OptionalHeader.SizeOfImage;
    g_sampler = CreateThread(NULL, 0, sampler, NULL, 0, NULL);
    if (!g_sampler) { abandon(NULL); return; }
    SetThreadPriority(g_sampler, THREAD_PRIORITY_ABOVE_NORMAL);
    atexit(hotsample_finish);
    LOG("[hotsample] sampling %zu functions into %s (top %llu, rewritten every %d s)\n", g_n, g_out, (unsigned long long)g_limit, WRITE_EVERY_MS / 1000);
}
#elif !defined(HOTSAMPLE_TEST)
void hotsample_thread(uintptr_t handle) { (void)handle; }
void hotsample_start(const GameLibTables *t) { (void)t; }
#endif
