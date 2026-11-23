// Movie audio: GetTrackConfiguration reports the codec's mime, and every FFmpeg sample format the decoders give
// (FLTP, FLT, S16, S16P, S32P, ...) comes out as non-silent interleaved s16. argv[1] is a directory holding
// vorbis.webm and opus.webm (a 440 Hz sine at 1/8 scale each, made by tools/test_movie_audio.py with ffmpeg, no game files).
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "../runtime/hle.h"
#undef HLE_FUNC
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#undef RT_ON_RESET
#define RT_ON_RESET(fn) static void fn(void)
bool romfs_file_info(const char *path, uint64_t *off, uint64_t *size) { (void)path, (void)off, (void)size; return false; }
ssize_t romfs_pread(void *dst, uint64_t n, uint64_t off) { (void)dst, (void)n, (void)off; return -1; }
#include "../runtime/hle/movie.c"

const char *g_romfs_dir;
Settings g_settings = {.movie_audio = true};
volatile int g_inject_plus;
uint64_t input_buttons_now(void) { return 0; }
uint64_t guest_sym(const char *name) { (void)name; return 0; }
void guest_call(Cpu *c, uint64_t address) { (void)c, (void)address; assert(!"No guest calls in the fixture"); }
void run_timer_ending_movie(void) {}
void run_timer_title(void) {}
void pace_hitch(const char *why) { (void)why; }

static int peak(const int16_t *s, int n) {
    int p = 0;
    for (int i = 0; i < n; i++) if (abs(s[i]) > p) p = abs(s[i]);
    return p;
}

// A 440 Hz stereo sine at half scale in format fmt, converted directly.
static void synthetic(enum AVSampleFormat fmt) {
    AVFrame *f = av_frame_alloc();
    f->format = fmt;
    f->nb_samples = 480;
    f->sample_rate = 48000;
    av_channel_layout_default(&f->ch_layout, 2);
    assert(av_frame_get_buffer(f, 0) == 0);
    bool planar = av_sample_fmt_is_planar(fmt);
    for (int i = 0; i < f->nb_samples; i++)
        for (int k = 0; k < 2; k++) {
            double v = 0.5 * sin(2 * M_PI * 440 * i / 48000.0);
            int plane = planar ? k : 0, idx = planar ? i : i * 2 + k;
            switch (fmt) {
                case AV_SAMPLE_FMT_FLT: case AV_SAMPLE_FMT_FLTP: ((float *)f->data[plane])[idx] = (float)v; break;
                case AV_SAMPLE_FMT_DBL: case AV_SAMPLE_FMT_DBLP: ((double *)f->data[plane])[idx] = v; break;
                case AV_SAMPLE_FMT_S16: case AV_SAMPLE_FMT_S16P: ((int16_t *)f->data[plane])[idx] = (int16_t)(v * 32767); break;
                case AV_SAMPLE_FMT_S32: case AV_SAMPLE_FMT_S32P: ((int32_t *)f->data[plane])[idx] = (int32_t)(v * 2147483647.0); break;
                default: assert(!"format not in the fixture");
            }
        }
    int16_t out[480 * 2];
    int n = movie_audio_to_s16(f, out, 480 * 2, false);
    int p = peak(out, n * 2);
    printf("%-5s %d samples, peak %d\n", av_get_sample_fmt_name(fmt), n, p);
    assert(n == 480 && p > 15000 && p < 17500);
    av_frame_free(&f);
}

// Open dir/name through the extractor, check the mime, decode its audio through emit_frame.
static void movie_file(const char *name, const char *mime) {
    char rel[64];
    snprintf(rel, sizeof rel, "rom:/%s", name);
    Extractor e = {0};
    uint64_t object[2] = {0, (uintptr_t)&e};
    Cpu c = {0};
    c.x[0] = (uintptr_t)object; c.x[2] = (uintptr_t)rel;
    ex_set_data_source(&c);
    assert(!c.x[0] && e.ntracks == 1);
    MediaData *m = NULL;
    c.x[0] = (uintptr_t)object; c.x[1] = 0; c.x[2] = (uintptr_t)&m;
    ex_get_track_config(&c);
    MdEntry *mt = md_slot(m, "mime");
    printf("%s: mime %s\n", name, mt->sval);
    assert(!c.x[0] && !strcmp(mt->sval, mime));

    AVCodecParameters *p = m->codecpar;
    Decoder d = {0};
    const AVCodec *codec = avcodec_find_decoder(p->codec_id);
    d.ctx = avcodec_alloc_context3(codec);
    assert(avcodec_parameters_to_context(d.ctx, p) == 0 && avcodec_open2(d.ctx, codec, NULL) == 0);
    d.out_cap = 64 << 10;
    d.out_mem[0] = malloc(d.out_cap);
    AVFrame *f = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    int best = 0, frames = 0;
    while (av_read_frame(e.fmt, pkt) >= 0) {
        if (avcodec_send_packet(d.ctx, pkt) == 0)
            while (avcodec_receive_frame(d.ctx, f) == 0) {
                assert(emit_frame(&d, f, 0, 0));
                int p = peak((int16_t *)d.out_mem[0], (int)(d.out_size[0] / 2));
                if (p > best) best = p;
                d.out_busy[0] = false; d.nready = 0;
                frames++;
            }
        av_packet_unref(pkt);
    }
    printf("%s: %s frames %d, peak %d\n", name, av_get_sample_fmt_name(d.ctx->sample_fmt), frames, best);
    assert(frames > 0 && best > 1000);
    av_packet_free(&pkt);
    av_frame_free(&f);
    free(d.out_mem[0]);
    avcodec_free_context(&d.ctx);
    md_clear(m); free(m);
    c.x[0] = (uintptr_t)object;
    ex_dtor(&c);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_romfs_dir = argc > 1 ? argv[1] : ".";  // usage: test [dir webm-name...]; no args = synthetic frames only
    enum AVSampleFormat fmts[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                  AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP};
    for (size_t i = 0; i < sizeof fmts / sizeof *fmts; i++) synthetic(fmts[i]);
    for (int i = 2; i < argc; i++) movie_file(argv[i], strstr(argv[i], "opus") ? "audio/opus" : "audio/vorbis");
    movie_reset();
    puts("PASS: movie audio mime per codec and s16 output for every sample format");
}
