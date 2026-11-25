/* Game-free check of the movie track metadata the guest uses to pace video.
 * NTSC frame rates are rational (30000/1001, 24000/1001, 60000/1001). The
 * host must not truncate them: the guest would step its picture clock slower
 * than the audio clock and drift for the length of the cutscene.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "runtime/rt.h"
#include "runtime/hle.h"
#include "runtime/settings.h"
#include "runtime/nvn/nvn.h"

void *hle_alloc(uint64_t size) { return calloc(1, size); }
uint64_t guest_sym(const char *name) { (void)name; return 0; }
void guest_call(Cpu *c, uint64_t addr) { (void)c; (void)addr; }
uint64_t hle_register(const char *name, HostFn fn) { (void)name; (void)fn; return 0; }
uint64_t input_buttons_now(void) { return 0; }
volatile int g_inject_plus;
const char *g_romfs_dir = "";
Settings g_settings;
uint64_t rt_ticks(void) { return 0; }
void run_timer_ending_movie(void) {}
void run_timer_title(void) {}
bool romfs_file_info(const char *path, uint64_t *off, uint64_t *size) { return false; }
ssize_t romfs_pread(void *dst, uint64_t off, uint64_t size) { return 0; }
void pace_hitch(const char *why) { (void)why; }
void renderer_upload_rgba(NvnTexture *t, const void *rgba, uint32_t w, uint32_t h, uint32_t stride) {
    (void)t; (void)rgba; (void)w; (void)h; (void)stride;
}
bool renderer_upload_nv12(NvnTexture *t, const void *nv12, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    (void)t; (void)nv12; (void)w; (void)h; (void)stride; (void)uv_off;
    return false;
}

#include "runtime/hle/movie.c"

static int track_fps(AVRational rate) {
    Extractor e = {0};
    AVFormatContext fmt = {0};
    AVStream st = {0};
    AVCodecParameters cp = {0};
    AVStream *streams[1] = {&st};

    cp.codec_type = AVMEDIA_TYPE_VIDEO;
    cp.codec_id = AV_CODEC_ID_VP9;
    cp.width = 1280;
    cp.height = 720;
    st.codecpar = &cp;
    st.avg_frame_rate = rate;
    fmt.streams = streams;
    fmt.nb_streams = 1;
    e.fmt = &fmt;
    e.map[0] = 0;
    e.ntracks = 1;

    uint64_t object[2] = {0, (uint64_t)&e};
    MediaData *metadata = NULL;
    Cpu c = {0};
    c.x[0] = (uintptr_t)object;      /* guest handle (slot 0) */
    c.x[1] = 0;                      /* track id */
    c.x[2] = (uintptr_t)&metadata;   /* guest slot for the metadata handle */
    ex_get_track_config(&c);
    assert(c.x[0] == 0);

    int fps = -1;
    for (int i = 0; i < metadata->n; i++) {
        if (metadata->e[i].key && strcmp(metadata->e[i].key, "frame-rate") == 0) {
            fps = (int)metadata->e[i].ival;
            break;
        }
    }
    free(metadata);
    return fps;
}

int main(void) {
    AVRational exact = {30, 1};
    AVRational ntsc30 = {30000, 1001};
    AVRational ntsc24 = {24000, 1001};
    AVRational ntsc60 = {60000, 1001};
    assert(track_fps(exact) == 30);
    assert(track_fps(ntsc30) == 30);
    assert(track_fps(ntsc24) == 24);
    assert(track_fps(ntsc60) == 60);
    puts("movie frame-rate metadata: ok");
    return 0;
}
