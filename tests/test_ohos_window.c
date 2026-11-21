// Drives video/out/ohos_common.c on the host, recording what it tells the
// NativeWindow: the HDR type, static and dynamic metadata each kind of stream
// ends up with, and when they are sent again.
#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video/out/ohos_common.c"

#include <libavutil/buffer.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hdr_dynamic_vivid_metadata.h>
#include <libplacebo/shaders/colorspace.h>

#include "test_util.h"
#include "video/dovi_tone_map.h"

// The mpv services ohos_common.c relies on.
struct mp_log { int unused; };
static struct mp_log test_log;

struct mp_log *mp_log_new(void *talloc_ctx, struct mp_log *parent,
                          const char *name)
{
    return &test_log;
}

// The Dolby Vision RPU description last logged, and how many were; the HDR
// Vivid and static metadata last logged.
static char dovi_log[256];
static int dovi_logs;
static char vivid_log[256];
static char static_log[256];

void mp_msg(struct mp_log *log, int lev, const char *format, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, sizeof(msg), format, ap);
    va_end(ap);
    if (strstr(msg, "Dolby Vision RPU")) {
        snprintf(dovi_log, sizeof(dovi_log), "%s", msg);
        dovi_logs++;
    }
    if (strstr(msg, "HDR Vivid metadata"))
        snprintf(vivid_log, sizeof(vivid_log), "%s", msg);
    if (strstr(msg, "HDR static metadata"))
        snprintf(static_log, sizeof(static_log), "%s", msg);
}

struct mp_image_params vo_get_current_params(struct vo *vo)
{
    return (struct mp_image_params){0};
}

// Copies of common/common.c and video/mp_image.c, which pull in far more than
// this test needs.
bool mp_rect_equals(const struct mp_rect *rc1, const struct mp_rect *rc2)
{
    return rc1->x0 == rc2->x0 && rc1->y0 == rc2->y0 &&
           rc1->x1 == rc2->x1 && rc1->y1 == rc2->y1;
}

bool mp_image_params_equal(const struct mp_image_params *p1,
                           const struct mp_image_params *p2)
{
    return p1->imgfmt == p2->imgfmt &&
           p1->hw_subfmt == p2->hw_subfmt &&
           p1->w == p2->w && p1->h == p2->h &&
           p1->p_w == p2->p_w && p1->p_h == p2->p_h &&
           p1->force_window == p2->force_window &&
           pl_color_space_equal(&p1->color, &p2->color) &&
           pl_color_repr_equal(&p1->repr, &p2->repr) &&
           p1->light == p2->light &&
           p1->chroma_location == p2->chroma_location &&
           p1->vflip == p2->vflip &&
           p1->rotate == p2->rotate &&
           p1->stereo3d == p2->stereo3d &&
           mp_rect_equals(&p1->crop, &p2->crop);
}

bool mp_image_params_static_equal(const struct mp_image_params *p1,
                                  const struct mp_image_params *p2)
{
    struct mp_image_params a = *p1;
    struct mp_image_params b = *p2;
    a.repr.dovi = b.repr.dovi = NULL;
    a.color.hdr = b.color.hdr = (struct pl_hdr_metadata){0};
    return mp_image_params_equal(&a, &b);
}

// The NativeWindow, keeping the last value of everything it was told.
static struct {
    OH_NativeBuffer_ColorSpace color_space;
    int32_t type;
    OH_NativeBuffer_StaticMetadata stat;
    uint8_t dyn[OHOS_VIVID_MAX_SIZE];
    size_t dyn_size;
    int metadata_calls;
} win;

int32_t OH_NativeWindow_CreateNativeWindowFromSurfaceId(uint64_t surface_id,
                                                        OHNativeWindow **window)
{
    static int dummy;
    *window = (OHNativeWindow *)&dummy;
    return 0;
}

void OH_NativeWindow_DestroyNativeWindow(OHNativeWindow *window)
{
}

int32_t OH_NativeWindow_SetColorSpace(OHNativeWindow *window,
                                      OH_NativeBuffer_ColorSpace color_space)
{
    win.color_space = color_space;
    return 0;
}

int32_t OH_NativeWindow_SetMetadataValue(OHNativeWindow *window,
                                         OH_NativeBuffer_MetadataKey key,
                                         int32_t size, uint8_t *value)
{
    win.metadata_calls++;
    if (key == OH_HDR_METADATA_TYPE)
        memcpy(&win.type, value, sizeof(win.type));
    if (key == OH_HDR_STATIC_METADATA)
        memcpy(&win.stat, value, sizeof(win.stat));
    if (key == OH_HDR_DYNAMIC_METADATA) {
        memcpy(win.dyn, value, size);
        win.dyn_size = size;
    }
    return 0;
}

int32_t OH_NativeWindow_NativeWindowHandleOpt(OHNativeWindow *window,
                                              int code, ...)
{
    return 0;
}

static float pq(float nits)
{
    return pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits);
}

// The 12 bit PQ code of a luminance.
static int pq_code(float nits)
{
    return lrintf(pq(nits) * 4095);
}

// The 12 bit maxRGB statistics of the dynamic metadata last sent.
static int dyn_avg(void)
{
    return (win.dyn[2] & 0xf) << 8 | win.dyn[3];
}

static int dyn_max(void)
{
    return (win.dyn[5] & 0xf) << 8 | win.dyn[6];
}

struct test_vo {
    struct vo vo; // first: ohos_common.c allocates its state under the vo
    struct mp_vo_opts opts;
};

static struct pl_color_map_params color_map;

static struct test_vo *new_vo(int mode)
{
    struct test_vo *t = talloc_zero(NULL, struct test_vo);
    t->opts.WinID = 1;
    t->opts.ohos_hdr_mode = mode;
    t->vo.opts = &t->opts;
    t->vo.log = &test_log;
    memset(&win, 0, sizeof(win));
    color_map = pl_color_map_default_params;
    color_map.tone_mapping_function = &pl_tone_map_spline;
    if (!vo_ohos_init(&t->vo)) {
        printf("vo_ohos_init failed\n");
        exit(1);
    }
    return t;
}

static void free_vo(struct test_vo *t)
{
    vo_ohos_uninit(&t->vo);
    talloc_free(t);
}

// What vo_gpu_next does with a frame: captures it, then resolves the
// colourspace hint, which becomes the render target. Returns the target.
static struct pl_color_space draw(struct test_vo *t, struct mp_image *mpi,
                                  uint64_t id, struct pl_color_space hint)
{
    struct vo_frame frame = {.current = mpi, .frame_id = id};
    vo_ohos_set_frame(&t->vo, &frame, &color_map);
    struct mp_image_params target = {.color = hint};
    vo_ohos_set_color(&t->vo, &target);
    return target.color;
}

// The colourspace hint vo_gpu_next builds from a stream (with the default
// --target-colorspace-hint-mode=target): the stream's own metadata with the
// target peak (--target-peak, 0 for none) in place of its mastering peak, the
// content light levels capped at it only when the mastering peak is above it.
static struct pl_color_space with_peak(struct pl_color_space source, float peak)
{
    struct pl_color_space hint = source;
    if (peak)
        hint.hdr.max_luma = peak;
    if (!hint.hdr.max_cll)
        hint.hdr.max_cll = hint.hdr.max_luma;
    if (source.hdr.max_luma > hint.hdr.max_luma) {
        if (!hint.hdr.max_cll || hint.hdr.max_luma < hint.hdr.max_cll)
            hint.hdr.max_cll = hint.hdr.max_luma;
        hint.hdr.max_fall = 0;
    }
    if (hint.hdr.max_cll && hint.hdr.max_fall > hint.hdr.max_cll)
        hint.hdr.max_fall = 0;
    return hint;
}

// Everything the NativeWindow was told about HDR, to compare two outputs.
struct window {
    int32_t type;
    OH_NativeBuffer_StaticMetadata stat;
    uint8_t dyn[OHOS_VIVID_MAX_SIZE];
    size_t dyn_size;
};

static struct window window_now(void)
{
    struct window w = {.type = win.type, .stat = win.stat,
                       .dyn_size = win.dyn_size};
    memcpy(w.dyn, win.dyn, win.dyn_size);
    return w;
}

static bool same_window(const struct window *a, const struct window *b)
{
    return a->type == b->type &&
           !memcmp(&a->stat, &b->stat, sizeof(a->stat)) &&
           a->dyn_size == b->dyn_size && !memcmp(a->dyn, b->dyn, a->dyn_size);
}

static struct mp_image_params pq_params(float mastering, float cll, float fall)
{
    return (struct mp_image_params){
        .imgfmt = 1, .w = 3840, .h = 2160, .p_w = 1, .p_h = 1,
        .color = {
            .primaries = PL_COLOR_PRIM_BT_2020,
            .transfer = PL_COLOR_TRC_PQ,
            .hdr = {.min_luma = 0.005f, .max_luma = mastering,
                    .max_cll = cll, .max_fall = fall},
        },
        .repr = {.sys = PL_COLOR_SYSTEM_BT_2020_NC,
                 .levels = PL_COLOR_LEVELS_LIMITED},
    };
}

static AVBufferRef *vivid_buf(AVDynamicHDRVivid *out)
{
    size_t size;
    AVDynamicHDRVivid *vivid = av_dynamic_hdr_vivid_alloc(&size);
    const struct ohos_vivid_stats st = {
        .min = pq(0.01f), .avg = pq(60), .var = 0.08f, .max = pq(400),
    };
    ohos_vivid_from_stats(vivid, &st);
    // A creator's curve, which only the stream's own metadata carries.
    AVHDRVividColorTransformParams *p = &vivid->params[0];
    p->tone_mapping_mode_flag = 1;
    p->tone_mapping_param_num = 1;
    p->tm_params[0].targeted_system_display_maximum_luminance = (AVRational){2000, 4095};
    p->tm_params[0].base_enable_flag = 1;
    p->tm_params[0].base_param_m_p = (AVRational){9000, 16383};
    p->tm_params[0].base_param_m_m = (AVRational){24, 10};
    p->tm_params[0].base_param_m_a = (AVRational){800, 1023};
    p->tm_params[0].base_param_m_b = (AVRational){3, 1023};
    p->tm_params[0].base_param_m_n = (AVRational){10, 10};
    *out = *vivid;
    return av_buffer_create((uint8_t *)vivid, size, NULL, NULL, 0);
}

// An RPU with level 1 alone, of the given luminance.
static AVBufferRef *dovi_buf(float avg_nits, float max_nits)
{
    const AVDOVIDmData blocks[] = {l1(0, pq_code(avg_nits), pq_code(max_nits))};
    return dovi_rpu(blocks, 1);
}

static void test_hdr10(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image img = {.params = pq_params(1000, 800, 200)};
    img.params.color.hdr.prim = *pl_raw_primaries_get(PL_COLOR_PRIM_DISPLAY_P3);

    static_log[0] = '\0';
    draw(t, &img, 1, img.params.color);
    CHECK(win.color_space == OH_COLORSPACE_DISPLAY_BT2020_PQ, "colour space");
    CHECK(win.type == OH_VIDEO_HDR_HDR10, "type %d", win.type);
    CHECK(win.stat.smpte2086.maxLuminance == 1000 &&
          win.stat.cta861.maxContentLightLevel == 800 &&
          win.stat.cta861.maxFrameAverageLightLevel == 200,
          "static metadata: %f %f %f", win.stat.smpte2086.maxLuminance,
          win.stat.cta861.maxContentLightLevel,
          win.stat.cta861.maxFrameAverageLightLevel);
    // The log shows the mastering primaries, which Dolby Vision level 9 may
    // have filled in.
    const char *want = "primaries R 0.6800,0.3200 G 0.2650,0.6900 "
                       "B 0.1500,0.0600 W 0.3127,0.3290\n";
    CHECK(strstr(static_log, want), "\"%s\" lacks \"%s\"", static_log, want);
    // Dynamic metadata matching the static one, replacing whatever an earlier
    // stream left on the surface.
    CHECK(win.dyn_size == 8 && win.dyn[0] == 1, "dynamic metadata");
    CHECK(dyn_max() == pq_code(800), "dynamic peak %d", dyn_max());

    // Only changes are sent again.
    int calls = win.metadata_calls;
    draw(t, &img, 1, img.params.color);
    draw(t, &img, 2, img.params.color);
    CHECK(win.metadata_calls == calls, "resent %d", win.metadata_calls - calls);

    // Until the window is recreated.
    vo_ohos_invalidate_color(&t->vo);
    draw(t, &img, 3, img.params.color);
    CHECK(win.metadata_calls == calls + 3, "%d sent after invalidation",
          win.metadata_calls - calls);
    free_vo(t);
}

static void test_peak_never_zero(void)
{
    // The render service grants no HDR headroom for a MaxCLL of 0.
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image img = {.params = pq_params(1000, 0, 0)};
    draw(t, &img, 1, img.params.color);
    CHECK(win.stat.cta861.maxContentLightLevel == 1000, "MaxCLL %f",
          win.stat.cta861.maxContentLightLevel);

    img.params.color = (struct pl_color_space){
        .primaries = PL_COLOR_PRIM_BT_2020, .transfer = PL_COLOR_TRC_HLG,
    };
    draw(t, &img, 2, with_peak(img.params.color, 400));
    CHECK(win.color_space == OH_COLORSPACE_DISPLAY_BT2020_HLG, "HLG space");
    CHECK(win.type == OH_VIDEO_HDR_HLG, "type %d", win.type);
    // HLG is adapted by the system, never compressed to --target-peak.
    CHECK(win.stat.cta861.maxContentLightLevel == 1000, "HLG MaxCLL %f",
          win.stat.cta861.maxContentLightLevel);
    free_vo(t);
}

static void test_native_vivid(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    AVDynamicHDRVivid vivid;
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DYNAMIC_HDR_VIVID, vivid_buf(&vivid)};
    struct mp_image img = {.params = pq_params(1000, 1000, 300),
                           .ff_side_data = &sd, .num_ff_side_data = 1};
    img.params.color.hdr.max_pq_y = pq(400);
    img.params.color.hdr.avg_pq_y = pq(60);

    // Passed through untouched: the stream's own metadata, curve included.
    draw(t, &img, 1, img.params.color);
    uint8_t own[OHOS_VIVID_MAX_SIZE];
    size_t own_size = ohos_vivid_write(&vivid, own, sizeof(own));
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "type %d", win.type);
    CHECK(win.dyn_size == own_size && !memcmp(win.dyn, own, own_size),
          "not the stream's own metadata (%zu bytes)", win.dyn_size);

    // Tone mapped: statistics describing the output, without the curve.
    draw(t, &img, 2, with_peak(img.params.color, 300));
    CHECK(win.dyn_size == 8, "%zu bytes", win.dyn_size);
    CHECK(abs(dyn_max() - pq_code(300)) <= 2, "peak %d vs %d", dyn_max(),
          pq_code(300));
    av_buffer_unref(&sd.buf);
    free_vo(t);
}

static void test_stream_change(void)
{
    // HDR Vivid, then plain HDR10 of the same size and format on the same VO,
    // as when switching between two HDR tiers.
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    AVDynamicHDRVivid vivid;
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DYNAMIC_HDR_VIVID, vivid_buf(&vivid)};
    struct mp_image vivid_img = {.params = pq_params(1000, 1000, 300),
                                 .ff_side_data = &sd, .num_ff_side_data = 1};
    struct mp_image plain = {.params = pq_params(1000, 1000, 300)};

    draw(t, &vivid_img, 1, vivid_img.params.color);
    draw(t, &plain, 2, plain.params.color);
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "kept within the stream");

    vo_ohos_reset(&t->vo);
    draw(t, &plain, 2, plain.params.color);
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "dropped on a redraw");
    draw(t, &plain, 14, plain.params.color);
    CHECK(win.type == OH_VIDEO_HDR_HDR10, "type %d after the change", win.type);
    CHECK(win.dyn_size == 8 && dyn_max() == pq_code(1000),
          "stale dynamic metadata (%zu bytes, peak %d)", win.dyn_size, dyn_max());
    av_buffer_unref(&sd.buf);
    free_vo(t);
}

// A surface in SDR forgets its HDR metadata: the static and dynamic metadata
// are sent again when HDR returns.
static void test_hdr_after_sdr(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image hdr = {.params = pq_params(1000, 800, 200)};
    struct mp_image sdr = {.params = pq_params(1000, 800, 200)};
    sdr.params.color = pl_color_space_srgb;

    draw(t, &hdr, 1, hdr.params.color);
    draw(t, &sdr, 2, sdr.params.color);
    CHECK(win.color_space == OH_COLORSPACE_DISPLAY_SRGB &&
          win.type == OH_VIDEO_NONE, "SDR colour space %d, type %d",
          win.color_space, win.type);

    memset(&win.stat, 0, sizeof(win.stat));
    win.dyn_size = 0;
    draw(t, &hdr, 3, hdr.params.color);
    CHECK(win.stat.cta861.maxContentLightLevel == 800, "static metadata %f",
          win.stat.cta861.maxContentLightLevel);
    CHECK(win.dyn_size == 8, "dynamic metadata (%zu bytes)", win.dyn_size);
    free_vo(t);
}

static void test_dolby_vision(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA, dovi_buf(100, 3000)};
    struct mp_image img = {.params = pq_params(4000, 0, 0),
                           .ff_side_data = &sd, .num_ff_side_data = 1};
    img.params.color.hdr.max_pq_y = pq(3000);
    img.params.color.hdr.avg_pq_y = pq(100);

    // Carried as HDR Vivid, at its own peak when matching the video...
    draw(t, &img, 1, img.params.color);
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "type %d", win.type);
    CHECK(abs(dyn_max() - pq_code(3000)) <= 1, "peak %d vs %d", dyn_max(),
          pq_code(3000));
    CHECK(abs(dyn_avg() - pq_code(100)) <= 1, "avg %d vs %d", dyn_avg(),
          pq_code(100));

    // ...and mapped to the target peak otherwise.
    draw(t, &img, 2, with_peak(img.params.color, 1000));
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "type %d", win.type);
    CHECK(win.stat.cta861.maxContentLightLevel == 1000, "MaxCLL %f",
          win.stat.cta861.maxContentLightLevel);
    CHECK(abs(dyn_max() - pq_code(1000)) <= 2, "peak %d vs %d", dyn_max(),
          pq_code(1000));
    CHECK(dyn_avg() <= pq_code(100) && dyn_avg() > 0, "avg %d", dyn_avg());

    // With Dolby's curve, which vo_gpu_next maps Dolby Vision by, the metadata
    // follows that curve: this shot has room for its mid tones, which stay.
    color_map.tone_mapping_function = &mp_dovi_tone_map;
    draw(t, &img, 3, with_peak(img.params.color, 1000));
    CHECK(abs(dyn_avg() - pq_code(100)) <= 1, "avg %d vs %d", dyn_avg(),
          pq_code(100));
    av_buffer_unref(&sd.buf);
    free_vo(t);
}

// What a Dolby Vision stream carries is logged once per stream, and again
// only when it changes, as level 3 may from shot to shot.
static void test_dolby_vision_logged(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA, dovi_buf(100, 3000)};
    struct mp_image img = {.params = pq_params(4000, 0, 0),
                           .ff_side_data = &sd, .num_ff_side_data = 1};
    dovi_logs = 0;

    draw(t, &img, 1, img.params.color);
    draw(t, &img, 2, img.params.color);
    CHECK(dovi_logs == 1 && strstr(dovi_log, "levels: 1\n"),
          "%d logs, last \"%s\"", dovi_logs, dovi_log);

    AVDOVIMetadata *md = (AVDOVIMetadata *)sd.buf->data;
    *av_dovi_get_ext(md, md->num_ext_blocks++) = (AVDOVIDmData){
        .level = 3,
        .l3 = {.min_pq_offset = 2048, .max_pq_offset = 1447, .avg_pq_offset = 2048},
    };
    draw(t, &img, 3, img.params.color);
    CHECK(dovi_logs == 2 && strstr(dovi_log, "3(min 2048 max 1447 avg 2048)"),
          "%d logs, last \"%s\"", dovi_logs, dovi_log);

    // A frame without an RPU has nothing to log.
    struct mp_image plain = {.params = img.params};
    draw(t, &plain, 4, plain.params.color);
    CHECK(dovi_logs == 2, "%d logs", dovi_logs);

    // A new stream logs its RPU afresh.
    struct mp_image other = img;
    other.params.w = 1920;
    draw(t, &other, 5, other.params.color);
    CHECK(dovi_logs == 3, "%d logs", dovi_logs);

    av_buffer_unref(&sd.buf);
    free_vo(t);
}

// Logged with the HDR Vivid metadata when libplacebo compresses the picture:
// the average and peak it tone maps by, so that a device log shows which
// luminance (Dolby Vision level 1 with level 3, say) reached the tone curve.
static void test_tone_map_logged(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA, dovi_buf(100, 3000)};
    struct mp_image img = {.params = pq_params(4000, 0, 0),
                           .ff_side_data = &sd, .num_ff_side_data = 1};
    img.params.color.hdr.max_pq_y = pq(3000);
    img.params.color.hdr.avg_pq_y = pq(150);

    vivid_log[0] = '\0';
    draw(t, &img, 1, img.params.color);
    CHECK(vivid_log[0] && !strstr(vivid_log, "tone mapped"), "\"%s\"",
          vivid_log);

    draw(t, &img, 2, with_peak(img.params.color, 1000));
    char want[64];
    snprintf(want, sizeof(want), ", tone mapped (spline) from avg %d max %d\n",
             pq_code(150), pq_code(3000));
    CHECK(strstr(vivid_log, want), "\"%s\" lacks \"%s\"", vivid_log, want);
    av_buffer_unref(&sd.buf);
    free_vo(t);
}

static void test_hdr10plus(void)
{
    // A dark scene from a real title; slots 5 and 10 are not percentiles.
    size_t size;
    AVDynamicHDRPlus *plus = av_dynamic_hdr_plus_alloc(&size);
    plus->num_windows = 1;
    AVHDRPlusColorTransformParams *p = &plus->params[0];
    for (int i = 0; i < 3; i++)
        p->maxscl[i] = (AVRational){6000, 100000};
    static const int pct[] = {1, 5, 10, 25, 50, 75, 90, 95, 99};
    static const int lin[] = {0, 2094, 96, 3, 4, 5, 88, 720, 2888};
    p->num_distribution_maxrgb_percentiles = 9;
    for (int i = 0; i < 9; i++) {
        p->distribution_maxrgb[i].percentage = pct[i];
        p->distribution_maxrgb[i].percentile = (AVRational){lin[i], 100000};
    }
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DYNAMIC_HDR_PLUS,
                                 av_buffer_create((uint8_t *)plus, size, NULL, NULL, 0)};
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image img = {.params = pq_params(1000, 1000, 400),
                           .ff_side_data = &sd, .num_ff_side_data = 1};

    draw(t, &img, 1, img.params.color);
    CHECK(win.type == OH_VIDEO_HDR_VIVID, "type %d", win.type);
    CHECK(dyn_max() == pq_code(600), "peak %d vs %d", dyn_max(), pq_code(600));
    CHECK(dyn_avg() < pq_code(10), "avg %d is not a dark scene", dyn_avg());
    av_buffer_unref(&sd.buf);
    free_vo(t);
}

// --target-peak only ever brings a stream down to it. A stream no brighter
// than the target reaches the window exactly as without one ("match video
// brightness"): its own static and dynamic metadata, nothing stretched to the
// target. libplacebo's target keeps the target peak, so that whatever turns
// out brighter than the metadata says is still compressed into it.
static void test_dim_stream_not_stretched(void)
{
    static const struct {
        const char *name;
        float mastering, cll, fall;
        float dovi_max; // a Dolby Vision RPU with this level 1 peak
        bool vivid;     // the stream's own HDR Vivid metadata
        bool untouched; // libplacebo maps nothing, by the metadata alone
    } streams[] = {
        {"HDR10", 1000, 800, 200, .untouched = true},
        {"HDR10 without MaxCLL", 1000, 0, 0, .untouched = true},
        // Graded on a brighter display than its content reaches: libplacebo
        // leaves it alone once it has measured it.
        {"HDR10 mastered brighter", 4000, 1500, 50},
        {"HDR10 reaching the target", 4000, 1600, 50},
        {"Dolby Vision", 1000, 1000, 400, .dovi_max = 800, .untouched = true},
        {"HDR Vivid", 1000, 1000, 300, .vivid = true, .untouched = true},
    };
    for (int i = 0; i < MP_ARRAY_SIZE(streams); i++) {
        const char *name = streams[i].name;
        struct mp_image img = {.params = pq_params(streams[i].mastering,
                                                   streams[i].cll,
                                                   streams[i].fall)};
        struct mp_ff_side_data sd = {0};
        AVDynamicHDRVivid vivid;
        if (streams[i].dovi_max) {
            sd = (struct mp_ff_side_data){AV_FRAME_DATA_DOVI_METADATA,
                                          dovi_buf(100, streams[i].dovi_max)};
            img.params.color.hdr.max_pq_y = pq(streams[i].dovi_max);
            img.params.color.hdr.avg_pq_y = pq(100);
        }
        if (streams[i].vivid) {
            sd = (struct mp_ff_side_data){AV_FRAME_DATA_DYNAMIC_HDR_VIVID,
                                          vivid_buf(&vivid)};
            img.params.color.hdr.max_pq_y = pq(400);
            img.params.color.hdr.avg_pq_y = pq(60);
        }
        if (sd.buf) {
            img.ff_side_data = &sd;
            img.num_ff_side_data = 1;
        }

        struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
        draw(t, &img, 1, with_peak(img.params.color, 0));
        struct window own = window_now();
        free_vo(t);

        t = new_vo(OHOS_HDR_MODE_AUTO);
        struct pl_color_space target =
            draw(t, &img, 1, with_peak(img.params.color, 1600));
        struct window mapped = window_now();
        CHECK(same_window(&own, &mapped), "%s: mastering %.0f vs %.0f, MaxCLL "
              "%.0f vs %.0f, MaxFALL %.0f vs %.0f, dynamic peak %d vs %d", name,
              mapped.stat.smpte2086.maxLuminance,
              own.stat.smpte2086.maxLuminance,
              mapped.stat.cta861.maxContentLightLevel,
              own.stat.cta861.maxContentLightLevel,
              mapped.stat.cta861.maxFrameAverageLightLevel,
              own.stat.cta861.maxFrameAverageLightLevel,
              (mapped.dyn[5] & 0xf) << 8 | mapped.dyn[6],
              (own.dyn[5] & 0xf) << 8 | own.dyn[6]);
        CHECK(target.hdr.max_luma == 1600, "%s: target peak %f", name,
              target.hdr.max_luma);
        CHECK(!streams[i].untouched ||
              pl_tone_map_params_noop(&t->vo.ohos->tone),
              "%s: tone mapped", name);
        free_vo(t);
        av_buffer_unref(&sd.buf);
    }
}

// A stream brighter than the target is compressed into it, and described so.
static void test_bright_stream_compressed(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image img = {.params = pq_params(4000, 0, 0)};
    draw(t, &img, 1, with_peak(img.params.color, 1600));
    CHECK(win.stat.smpte2086.maxLuminance == 1600 &&
          win.stat.cta861.maxContentLightLevel == 1600 &&
          win.stat.cta861.maxFrameAverageLightLevel == 0,
          "static metadata: %f %f %f", win.stat.smpte2086.maxLuminance,
          win.stat.cta861.maxContentLightLevel,
          win.stat.cta861.maxFrameAverageLightLevel);
    CHECK(dyn_max() == pq_code(1600), "dynamic peak %d", dyn_max());
    CHECK(!pl_tone_map_params_noop(&t->vo.ohos->tone), "not tone mapped");
    free_vo(t);
}

// libplacebo measures a stream it maps to a target peak (--hdr-compute-peak).
// Where the stream turns out brighter than its metadata says, what it measured
// bounds the window's light levels, until the next seek or stream.
static void test_measured_peak(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    struct mp_image img = {.params = pq_params(1000, 0, 0)};
    struct pl_color_space hint = with_peak(img.params.color, 1600);
    const OH_NativeBuffer_StaticMetadata *md = &win.stat;

    draw(t, &img, 1, hint);
    CHECK(md->cta861.maxContentLightLevel == 1000, "MaxCLL %f",
          md->cta861.maxContentLightLevel);

    // Brighter than labelled, still within the target: passed untouched.
    vo_ohos_set_measured_peak(&t->vo, pq(1200));
    draw(t, &img, 2, hint);
    CHECK(fabsf(md->cta861.maxContentLightLevel - 1200) < 1 &&
          md->smpte2086.maxLuminance == 1000, "MaxCLL %f, mastering %f",
          md->cta861.maxContentLightLevel, md->smpte2086.maxLuminance);

    // Brighter than the target: compressed into it.
    vo_ohos_set_measured_peak(&t->vo, pq(4000));
    draw(t, &img, 3, hint);
    CHECK(md->cta861.maxContentLightLevel == 1600 &&
          md->smpte2086.maxLuminance == 1600, "MaxCLL %f, mastering %f",
          md->cta861.maxContentLightLevel, md->smpte2086.maxLuminance);
    CHECK(dyn_max() == pq_code(1600), "dynamic peak %d", dyn_max());

    // The brightest so far holds for the rest of the stream.
    vo_ohos_set_measured_peak(&t->vo, pq(500));
    draw(t, &img, 4, hint);
    CHECK(md->cta861.maxContentLightLevel == 1600, "MaxCLL %f after a dimmer "
          "scene", md->cta861.maxContentLightLevel);

    // libplacebo measures afresh after a seek...
    vo_ohos_reset(&t->vo);
    draw(t, &img, 5, hint);
    CHECK(md->cta861.maxContentLightLevel == 1000, "MaxCLL %f after a seek",
          md->cta861.maxContentLightLevel);

    // ...and for a new stream.
    vo_ohos_set_measured_peak(&t->vo, pq(4000));
    struct mp_image other = img;
    other.params.w = 1920;
    draw(t, &other, 6, hint);
    CHECK(md->cta861.maxContentLightLevel == 1000, "MaxCLL %f for a new stream",
          md->cta861.maxContentLightLevel);

    // Dolby Vision and HDR Vivid carry each frame's own peak, which libplacebo
    // maps them by instead of measuring them.
    struct mp_image own_peak = img;
    own_peak.params.color.hdr.max_pq_y = pq(1200);
    own_peak.params.color.hdr.avg_pq_y = pq(100);
    draw(t, &own_peak, 7, hint);
    CHECK(fabsf(md->cta861.maxContentLightLevel - 1200) < 1 &&
          md->smpte2086.maxLuminance == 1000, "MaxCLL %f, mastering %f for a "
          "frame's own peak", md->cta861.maxContentLightLevel,
          md->smpte2086.maxLuminance);
    free_vo(t);
}

// Asked to expand streams (--inverse-tone-mapping), libplacebo maps them up to
// the target, which the window then describes.
static void test_inverse_tone_mapping(void)
{
    struct test_vo *t = new_vo(OHOS_HDR_MODE_AUTO);
    color_map.inverse_tone_mapping = true;
    struct mp_image img = {.params = pq_params(1000, 800, 200)};
    // vo_gpu_next's hint takes the target's light levels for it.
    struct pl_color_space hint = with_peak(img.params.color, 1600);
    hint.hdr.max_cll = 1600;
    hint.hdr.max_fall = 0;

    draw(t, &img, 1, hint);
    CHECK(win.stat.smpte2086.maxLuminance == 1600 &&
          win.stat.cta861.maxContentLightLevel == 1600,
          "mastering %f, MaxCLL %f", win.stat.smpte2086.maxLuminance,
          win.stat.cta861.maxContentLightLevel);
    CHECK(!pl_tone_map_params_noop(&t->vo.ohos->tone), "not expanded");
    free_vo(t);
}

static void test_modes(void)
{
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA, dovi_buf(100, 3000)};
    struct mp_image img = {.params = pq_params(1000, 1000, 400),
                           .ff_side_data = &sd, .num_ff_side_data = 1};

    // Panels without HDR Vivid get static metadata only.
    struct test_vo *t = new_vo(OHOS_HDR_MODE_HDR10);
    draw(t, &img, 1, img.params.color);
    CHECK(win.type == OH_VIDEO_HDR_HDR10, "hdr10 mode type %d", win.type);
    CHECK(win.stat.cta861.maxContentLightLevel == 1000, "hdr10 mode MaxCLL");
    free_vo(t);

    // No HDR output: the surface stays SDR and libplacebo maps the stream.
    t = new_vo(OHOS_HDR_MODE_OFF);
    struct pl_color_space target = draw(t, &img, 1, img.params.color);
    CHECK(win.color_space == OH_COLORSPACE_DISPLAY_SRGB, "SDR colour space");
    CHECK(win.type == OH_VIDEO_NONE, "type %d", win.type);
    CHECK(target.transfer == PL_COLOR_TRC_SRGB, "target transfer %d",
          target.transfer);
    free_vo(t);
    av_buffer_unref(&sd.buf);
}

int main(void)
{
    test_hdr10();
    test_peak_never_zero();
    test_native_vivid();
    test_stream_change();
    test_hdr_after_sdr();
    test_dolby_vision();
    test_dolby_vision_logged();
    test_tone_map_logged();
    test_hdr10plus();
    test_dim_stream_not_stretched();
    test_bright_stream_compressed();
    test_measured_peak();
    test_inverse_tone_mapping();
    test_modes();
    return report();
}
