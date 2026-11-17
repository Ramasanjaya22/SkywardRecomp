#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvn/shader_prep.h"

int main(void) {
    ShaderRec r = {0};
    r.kind = 'G'; r.vs = 1; r.fs = 2; r.logic_op = 7;  // NVN OR
    char line[1024];
    shader_rec_format(&r, line, sizeof line);
    assert(strstr(line, " o=7"));

    ShaderRec p;
    assert(shader_rec_parse(line, &p));
    assert(p.logic_op == 7);  // round-trip

    char legacy[1024];
    snprintf(legacy, sizeof legacy, "%.*s", (int)(strstr(line, " o=") - line), line);
    assert(shader_rec_parse(legacy, &p));
    assert(p.logic_op == 3);  // older records without o= keep NVN COPY
    return 0;
}
