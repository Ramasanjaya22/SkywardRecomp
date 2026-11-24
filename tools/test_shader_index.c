// (tools/test_shader_index.py) included after shader_prep.c's index section
#include <assert.h>

size_t rt_lz4_decompress(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    (void)src, (void)srclen, (void)dst, (void)dstlen;
    return 0;
}

static void check(const char *hash, const char *code_path, FILE **files) {
    const IdxEnt *e = idx_find(strtoull(hash, NULL, 16));
    assert(e && e->size == 0x60 && e->const_offset == 0x40 && e->const_size == 0x20);
    uint8_t want[0x60];
    FILE *f = fopen(code_path, "rb");
    assert(f && fread(want, 1, sizeof want, f) == sizeof want);
    fclose(f);
    ShaderBlob b;
    uint8_t *d = blob_read(e, &b, files);
    assert(d && !memcmp(d, want, sizeof want));  // read back from the image, at the file's place in it
    free(d);
}

int main(int argc, char **argv) {
    assert(argc == 6);
    idx_game(argv[1]);  // (argv[1]/romfs doesn't exist: argv[1].bin is the game files)
    printf("indexed %zu shaders in %d files\n", idx_n, idx_nfiles);
    assert(idx_n == 2 && idx_nfiles == 2);
    assert(idx_files[1].fh == idx_files[0].fh);  // (one open handle for the image)
    FILE **files = calloc((size_t)idx_nfiles + 1, sizeof *files);
    check(argv[2], argv[4], files);
    check(argv[3], argv[5], files);
    return 0;
}
