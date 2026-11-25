// When the launcher keeps an existing extraction (runtime/ui/extract_current.h). tools/test_extract_current.py.
#include <stdio.h>

#include "ui/extract_current.h"

static int failures;
#define CHECK(c)                                                                                 \
    do {                                                                                         \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
    } while (0)

static const char *MANIFEST =
    "{\n  \"source\": \"I:\\\\Games\\\\rom\\\\Zelda\\\\Zelda [01002DA013484800][v65536].nsp\",\n"
    "  \"source_size\": 42871096,\n  \"source_mtime_ns\": 1779253468234878400,\n  \"container\": \"nsp\",\n"
    "  \"update\": true,\n  \"files\": []\n}\n";
static const char *INDEX = "{\n  \"status\": \"pass\",\n  \"source_size\": 42871096,\n  \"romfs_size\": 7552117756,\n  \"files\": []\n}\n";
static const char *NAME = "Zelda [01002DA013484800][v65536].nsp";

int main(void) {
    CHECK(extraction_current(MANIFEST, INDEX, 7552117756ull, NAME, 42871096ull, 1));  // the same package: kept
    CHECK(!extraction_current(MANIFEST, INDEX, 7552117756ull, NAME, 42871097ull, 1));  // the package changed size
    CHECK(!extraction_current(MANIFEST, INDEX, 7552117756ull, "Other.nsp", 42871096ull, 1));  // another package
    CHECK(!extraction_current(MANIFEST, INDEX, 7552117756ull, NAME, 42871096ull, 0));  // base-only now, update before
    CHECK(!extraction_current(MANIFEST, INDEX, 7000000000ull, NAME, 42871096ull, 1));  // romfs.bin cut short
    CHECK(!extraction_current(MANIFEST, INDEX, 0, NAME, 42871096ull, 1));
    CHECK(!extraction_current(NULL, INDEX, 7552117756ull, NAME, 42871096ull, 1));  // no manifest / index
    CHECK(!extraction_current(MANIFEST, NULL, 7552117756ull, NAME, 42871096ull, 1));
    CHECK(!extraction_current("{}", INDEX, 7552117756ull, NAME, 42871096ull, 1));
    // a base-only extraction (update false), same base package
    const char *base = "{\"source\": \"/roms/Zelda base.nsp\", \"source_size\": 5, \"update\": false}";
    CHECK(extraction_current(base, "{\"romfs_size\": 9}", 9, "Zelda base.nsp", 5, 0));
    CHECK(!extraction_current(base, "{\"romfs_size\": 9}", 9, "Zelda base.nsp", 5, 1));
    // the name is the whole file name, not a tail of it
    CHECK(!extraction_current(base, "{\"romfs_size\": 9}", 9, "base.nsp", 5, 0));
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
