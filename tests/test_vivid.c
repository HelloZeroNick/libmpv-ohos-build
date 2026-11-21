// Checks video/out/ohos_hdr_vivid.c against FFmpeg's own HDR Vivid parser:
// serialization round trips, statistics extraction and tone mapping.
#include <math.h>
#include <string.h>

#include <libavutil/buffer.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hdr_dynamic_vivid_metadata.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/tone_mapping.h>

#include "common/common.h"
#include "video/mp_image.h"
#include "video/out/ohos_hdr_vivid.h"
#include "test_util.h"

// libavcodec/dynamic_hdr_vivid.h, internal to FFmpeg.
int ff_parse_itu_t_t35_to_dynamic_hdr_vivid(AVDynamicHDRVivid *s,
                                             const uint8_t *data, int size);

static bool q_eq(AVRational a, AVRational b)
{
    if (!a.num && !b.num)
        return true;
    return a.den && b.den && av_cmp_q(a, b) == 0;
}

static float pq(float nits)
{
    return pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits);
}

static AVDynamicHDRVivid full_vivid(void)
{
    AVDynamicHDRVivid v = {.system_start_code = 0x01, .num_windows = 1};
    AVHDRVividColorTransformParams *p = &v.params[0];
    p->minimum_maxrgb = (AVRational){12, 4095};
    p->average_maxrgb = (AVRational){1500, 4095};
    p->variance_maxrgb = (AVRational){700, 4095};
    p->maximum_maxrgb = (AVRational){3000, 4095};
    p->tone_mapping_mode_flag = 1;
    p->tone_mapping_param_num = 2;
    for (int i = 0; i < 2; i++) {
        AVHDRVividColorToneMappingParams *tm = &p->tm_params[i];
        tm->targeted_system_display_maximum_luminance = (AVRational){2000 + 500 * i, 4095};
        tm->base_enable_flag = 1;
        tm->base_param_m_p = (AVRational){9000 + i, 16383};
        tm->base_param_m_m = (AVRational){24 + i, 10};
        tm->base_param_m_a = (AVRational){800 + i, 1023};
        tm->base_param_m_b = (AVRational){3 + i, 1023};
        tm->base_param_m_n = (AVRational){10 + i, 10};
        tm->base_param_k1 = 1;
        tm->base_param_k2 = 2 - i;
        tm->base_param_k3 = 9 + i;
        tm->base_param_Delta_enable_mode = 3 + i;
        tm->base_param_Delta = (AVRational){50 + i, 127};
        tm->three_Spline_enable_flag = 1;
        tm->three_Spline_num = 2;
        for (int j = 0; j < 2; j++) {
            AVHDRVivid3SplineParams *s = &tm->three_spline[j];
            // th_mode 0 carries th_enable_mb, th_mode 1 does not.
            s->th_mode = j;
            s->th_enable_mb = (AVRational){j ? 0 : 77, 255};
            s->th_enable = (AVRational){1000 + j, 4095};
            s->th_delta1 = (AVRational){100 + j, 1023};
            s->th_delta2 = (AVRational){200 + j, 1023};
            s->enable_strength = (AVRational){128 + j, 255};
        }
    }
    p->color_saturation_mapping_flag = 1;
    p->color_saturation_num = 7;
    for (int i = 0; i < 7; i++)
        p->color_saturation_gain[i] = (AVRational){100 + i, 128};
    return v;
}

static void compare_vivid(const AVDynamicHDRVivid *a, const AVDynamicHDRVivid *b)
{
    CHECK(a->system_start_code == b->system_start_code, "system_start_code");
    CHECK(a->num_windows == b->num_windows, "num_windows");
    const AVHDRVividColorTransformParams *p = &a->params[0], *q = &b->params[0];
    CHECK(q_eq(p->minimum_maxrgb, q->minimum_maxrgb), "minimum_maxrgb");
    CHECK(q_eq(p->average_maxrgb, q->average_maxrgb), "average_maxrgb");
    CHECK(q_eq(p->variance_maxrgb, q->variance_maxrgb), "variance_maxrgb");
    CHECK(q_eq(p->maximum_maxrgb, q->maximum_maxrgb), "maximum_maxrgb");
    CHECK(p->tone_mapping_mode_flag == q->tone_mapping_mode_flag, "tm flag");
    if (p->tone_mapping_mode_flag) {
        CHECK(p->tone_mapping_param_num == q->tone_mapping_param_num, "tm num");
        for (int i = 0; i < p->tone_mapping_param_num; i++) {
            const AVHDRVividColorToneMappingParams *x = &p->tm_params[i],
                                                   *y = &q->tm_params[i];
            CHECK(q_eq(x->targeted_system_display_maximum_luminance,
                       y->targeted_system_display_maximum_luminance), "tm%d target", i);
            CHECK(x->base_enable_flag == y->base_enable_flag, "tm%d base flag", i);
            CHECK(q_eq(x->base_param_m_p, y->base_param_m_p), "tm%d m_p", i);
            CHECK(q_eq(x->base_param_m_m, y->base_param_m_m), "tm%d m_m", i);
            CHECK(q_eq(x->base_param_m_a, y->base_param_m_a), "tm%d m_a", i);
            CHECK(q_eq(x->base_param_m_b, y->base_param_m_b), "tm%d m_b", i);
            CHECK(q_eq(x->base_param_m_n, y->base_param_m_n), "tm%d m_n", i);
            CHECK(x->base_param_k1 == y->base_param_k1, "tm%d k1", i);
            CHECK(x->base_param_k2 == y->base_param_k2, "tm%d k2", i);
            CHECK(x->base_param_k3 == y->base_param_k3, "tm%d k3", i);
            CHECK(x->base_param_Delta_enable_mode == y->base_param_Delta_enable_mode,
                  "tm%d delta mode", i);
            CHECK(q_eq(x->base_param_Delta, y->base_param_Delta), "tm%d delta", i);
            CHECK(x->three_Spline_enable_flag == y->three_Spline_enable_flag,
                  "tm%d spline flag", i);
            CHECK(x->three_Spline_num == y->three_Spline_num, "tm%d spline num", i);
            for (int j = 0; j < x->three_Spline_num; j++) {
                const AVHDRVivid3SplineParams *s = &x->three_spline[j],
                                              *t = &y->three_spline[j];
                CHECK(s->th_mode == t->th_mode, "tm%d spline%d mode", i, j);
                CHECK(q_eq(s->th_enable_mb, t->th_enable_mb), "tm%d spline%d mb", i, j);
                CHECK(q_eq(s->th_enable, t->th_enable), "tm%d spline%d enable", i, j);
                CHECK(q_eq(s->th_delta1, t->th_delta1), "tm%d spline%d delta1", i, j);
                CHECK(q_eq(s->th_delta2, t->th_delta2), "tm%d spline%d delta2", i, j);
                CHECK(q_eq(s->enable_strength, t->enable_strength),
                      "tm%d spline%d strength", i, j);
            }
        }
    }
    CHECK(p->color_saturation_mapping_flag == q->color_saturation_mapping_flag,
          "saturation flag");
    CHECK(p->color_saturation_num == q->color_saturation_num, "saturation num");
    for (int i = 0; i < p->color_saturation_num; i++)
        CHECK(q_eq(p->color_saturation_gain[i], q->color_saturation_gain[i]),
              "saturation gain %d", i);
}

static size_t write_and_parse(const AVDynamicHDRVivid *in, AVDynamicHDRVivid *out)
{
    uint8_t buf[OHOS_VIVID_MAX_SIZE];
    size_t size = ohos_vivid_write(in, buf, sizeof(buf));
    CHECK(size > 0, "write returned %zu", size);
    if (!size)
        return size;
    // HarmonyOS takes dynamic_metadata() itself, starting at system_start_code.
    CHECK(buf[0] == in->system_start_code, "starts with 0x%02x", buf[0]);
    memset(out, 0, sizeof(*out));
    int ret = ff_parse_itu_t_t35_to_dynamic_hdr_vivid(out, buf, size);
    CHECK(ret == 0, "FFmpeg rejected the payload: %d", ret);
    return size;
}

static void test_full_round_trip(void)
{
    AVDynamicHDRVivid in = full_vivid(), out;
    size_t size = write_and_parse(&in, &out);
    CHECK(size <= OHOS_VIVID_MAX_SIZE, "size %zu", size);
    compare_vivid(&in, &out);
}

static void test_stats_round_trip(void)
{
    struct ohos_vivid_stats st = {.min = 0.01f, .avg = 0.35f, .var = 0.12f, .max = 0.75f};
    AVDynamicHDRVivid in, out;
    ohos_vivid_from_stats(&in, &st);
    size_t size = write_and_parse(&in, &out);
    // 8 bit start code, 4 x 12 bit statistics and two flags: 58 bits.
    CHECK(size == 8, "minimal payload is %zu bytes", size);
    compare_vivid(&in, &out);
    const AVHDRVividColorTransformParams *p = &out.params[0];
    CHECK(p->minimum_maxrgb.num == 41, "min code %d", p->minimum_maxrgb.num);
    CHECK(p->average_maxrgb.num == 1433, "avg code %d", p->average_maxrgb.num);
    CHECK(p->variance_maxrgb.num == 491, "var code %d", p->variance_maxrgb.num);
    CHECK(p->maximum_maxrgb.num == 3071, "max code %d", p->maximum_maxrgb.num);
    CHECK(!p->tone_mapping_mode_flag && !p->color_saturation_mapping_flag, "flags");
}

static void test_stats_are_ordered(void)
{
    // Out of range and out of order input must still serialize sanely.
    struct ohos_vivid_stats st = {.min = 0.9f, .avg = 1.5f, .var = 2.0f, .max = 0.5f};
    AVDynamicHDRVivid in, out;
    ohos_vivid_from_stats(&in, &st);
    write_and_parse(&in, &out);
    const AVHDRVividColorTransformParams *p = &out.params[0];
    CHECK(p->minimum_maxrgb.num <= p->average_maxrgb.num &&
          p->average_maxrgb.num <= p->maximum_maxrgb.num, "min <= avg <= max");
    CHECK(p->variance_maxrgb.num <= 4095, "var in range");
}

static void test_rejects_unknown_versions(void)
{
    AVDynamicHDRVivid v = full_vivid();
    uint8_t buf[OHOS_VIVID_MAX_SIZE];
    v.system_start_code = 0x08;
    CHECK(!ohos_vivid_write(&v, buf, sizeof(buf)), "start code 0x08 accepted");
    v.system_start_code = 0x00;
    CHECK(!ohos_vivid_write(&v, buf, sizeof(buf)), "start code 0x00 accepted");
    v.system_start_code = 0x01;
    CHECK(!ohos_vivid_write(&v, buf, 20), "short buffer accepted");
}

static struct mp_image image_with(struct mp_ff_side_data *sd, int num)
{
    return (struct mp_image){.ff_side_data = sd, .num_ff_side_data = num};
}

// An RPU with level 1 alone.
static AVBufferRef *dovi_l1(int min, int avg, int max)
{
    const AVDOVIDmData blocks[] = {l1(min, avg, max)};
    return dovi_rpu(blocks, 1);
}

static struct ohos_vivid_stats probe_dovi(const AVDOVIDmData *blocks, int num)
{
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA,
                                 dovi_rpu(blocks, num)};
    struct mp_image img = image_with(&sd, 1);
    struct ohos_vivid_stats st = {0};
    const AVDynamicHDRVivid *native;
    CHECK(ohos_vivid_probe(&img, &st, &native) == OHOS_DYNAMIC_HDR_DOVI, "kind");
    CHECK(!native, "no native payload");
    av_buffer_unref(&sd.buf);
    return st;
}

// HDR Vivid gets the shot mp_dovi_shot() makes of the RPU (see test_dovi_hdr):
// level 1 moved by level 3, put back in order, the peak not below level 1's
// and the average off black.
static void test_probe_dovi(void)
{
    const struct {
        AVDOVIDmData l1, l3;    // level 3 only where its level is not 0
        float min, avg, max;
    } cases[] = {
        {l1(62, 1229, 3079), {0},
         code(62), code(1229), code(3079)},
        {l1(62, 1229, 3079), l3(20, -100, 300),
         code(62) + offset(20), code(1229) - offset(100),
         code(3079) + offset(300)},
        // A peak under level 1's is dropped; the other offsets still apply.
        {l1(62, 1229, 3079), l3(20, 100, -300),
         code(62) + offset(20), code(1229) + offset(100), code(3079)},
        // Offsets past one another or past either end of the PQ range.
        {l1(100, 1000, 3000), l3(0, 0, -2048),
         code(100), code(1000), code(3000)},
        {l1(100, 1000, 3000), l3(0, 2047, 0),
         code(100), code(3000), code(3000)},
        {l1(100, 1000, 3000), l3(0, 0, 2047), code(100), code(1000), 1},
        {l1(100, 1000, 3000), l3(-2048, 0, 0), 0, code(1000), code(3000)},
        {l1(100, 1000, 3000), l3(2047, 0, 0),
         code(3000), code(3000), code(3000)},
        // A shot recorded on a device whose mid tone offset reaches black.
        {l1(0, 1706, 3344), l3(0, 1195 - 2048, 0), 0, code(1), code(3344)},
    };
    for (int i = 0; i < MP_ARRAY_SIZE(cases); i++) {
        const AVDOVIDmData blocks[] = {cases[i].l1, cases[i].l3};
        struct ohos_vivid_stats st = probe_dovi(blocks,
                                                cases[i].l3.level ? 2 : 1);
        // The spread is there unless the picture is flat, and within half
        // its range (plus the rounding noise a flat one leaves).
        CHECK(near(st.min, cases[i].min) && near(st.avg, cases[i].avg) &&
              near(st.max, cases[i].max) &&
              (st.var > 0 || st.max == st.min) &&
              st.var <= (st.max - st.min) / 2 + 1e-3,
              "case %d: min %f avg %f max %f var %f",
              i, st.min, st.avg, st.max, st.var);
    }
}

// The log line naming what a Dolby Vision RPU carries: its levels in order,
// repeated ones counted, and the raw level 3 offsets.
static void test_dovi_describe(void)
{
    const AVDOVIDmData blocks[] = {
        {.level = 6}, {.level = 1, .l1 = {.max_pq = 3079}},
        {.level = 2}, {.level = 2}, {.level = 8}, {.level = 9},
        {.level = 3, .l3 = {.min_pq_offset = 2048, .max_pq_offset = 1447,
                            .avg_pq_offset = 2100}},
        {.level = 254},
    };
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DOVI_METADATA,
                                 dovi_rpu(blocks, MP_ARRAY_SIZE(blocks))};
    struct mp_image img = image_with(&sd, 1);
    char buf[96];
    ohos_dovi_describe(&img, buf, sizeof(buf));
    const char *want = "1 2x2 3(min 2048 max 1447 avg 2100) 6 8 9 254";
    CHECK(!strcmp(buf, want), "\"%s\"", buf);

    // A buffer too small for it still ends in a terminator.
    char tiny[8];
    memset(tiny, 'x', sizeof(tiny));
    ohos_dovi_describe(&img, tiny, sizeof(tiny));
    CHECK(!strncmp(tiny, want, sizeof(tiny) - 1) && !tiny[sizeof(tiny) - 1],
          "\"%.8s\"", tiny);
    av_buffer_unref(&sd.buf);

    struct mp_image plain = {0};
    ohos_dovi_describe(&plain, buf, sizeof(buf));
    CHECK(!buf[0], "\"%s\" for a frame without an RPU", buf);
}

// HDR10+ side data for one window, in units of 1/100000 of 10000 nits. As in
// real HDR10+ metadata, the 5% and 10% slots hold the 99.99th percentile and
// the share of pixels under 100 nits rather than those percentiles, and 99
// stands for the 99.98th percentile.
static AVBufferRef *hdr10plus(int maxscl, const int lin[9])
{
    size_t size;
    AVDynamicHDRPlus *plus = av_dynamic_hdr_plus_alloc(&size);
    plus->num_windows = 1;
    AVHDRPlusColorTransformParams *p = &plus->params[0];
    p->maxscl[0] = (AVRational){maxscl * 7 / 8, 100000};
    p->maxscl[1] = (AVRational){maxscl, 100000};
    p->maxscl[2] = (AVRational){maxscl / 2, 100000};
    p->average_maxrgb = (AVRational){1000, 100000};
    static const int pct[] = {1, 5, 10, 25, 50, 75, 90, 95, 99};
    p->num_distribution_maxrgb_percentiles = 9;
    for (int i = 0; i < 9; i++) {
        p->distribution_maxrgb[i].percentage = pct[i];
        p->distribution_maxrgb[i].percentile = (AVRational){lin[i], 100000};
    }
    return av_buffer_create((uint8_t *)plus, size, NULL, NULL, 0);
}

static enum ohos_dynamic_hdr probe_hdr10plus(AVBufferRef *buf,
                                             struct ohos_vivid_stats *st)
{
    struct mp_ff_side_data sd = {AV_FRAME_DATA_DYNAMIC_HDR_PLUS, buf};
    struct mp_image img = image_with(&sd, 1);
    const AVDynamicHDRVivid *native;
    enum ohos_dynamic_hdr kind = ohos_vivid_probe(&img, st, &native);
    CHECK(!native, "no native payload");
    av_buffer_unref(&sd.buf);
    return kind;
}

static void test_probe_hdr10plus(void)
{
    static const int lin[] = {5, 30000, 40, 200, 600, 1500, 4000, 9000, 25000};
    struct ohos_vivid_stats st;
    CHECK(probe_hdr10plus(hdr10plus(40000, lin), &st) == OHOS_DYNAMIC_HDR_PLUS,
          "kind");
    CHECK(fabsf(st.max - pq(4000)) < 1e-4, "max %f vs %f", st.max, pq(4000));
    CHECK(fabsf(st.min - pq(0.5f)) < 1e-4, "min %f vs %f", st.min, pq(0.5f));
    // The median is 60 nits; a PQ domain mean sits near it, well below the
    // peak, whatever the 99.99th percentile in the 5% slot says.
    CHECK(st.avg > pq(20) && st.avg < pq(400), "avg %f", st.avg);
    CHECK(st.var > 0.02f && st.var < 0.3f, "var %f", st.var);
}

static void test_hdr10plus_dark_scene(void)
{
    // A dark scene from a real title: 90% of it under 9 nits.
    static const int lin[] = {0, 2094, 96, 3, 4, 5, 88, 720, 2888};
    struct ohos_vivid_stats st;
    CHECK(probe_hdr10plus(hdr10plus(6000, lin), &st) == OHOS_DYNAMIC_HDR_PLUS,
          "kind");
    CHECK(fabsf(st.max - pq(600)) < 1e-4, "max %f vs %f", st.max, pq(600));
    CHECK(st.avg < pq(10), "avg %f is %f nits, not a dark scene", st.avg,
          pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, st.avg));
    CHECK(st.min <= st.avg && st.avg <= st.max, "ordered");

    // Without MaxSCL the brightest histogram value is the peak, as libplacebo
    // takes it.
    CHECK(probe_hdr10plus(hdr10plus(0, lin), &st) == OHOS_DYNAMIC_HDR_PLUS,
          "kind without MaxSCL");
    CHECK(fabsf(st.max - pq(288.8f)) < 1e-4, "max %f vs %f", st.max, pq(288.8f));
}

static void test_hdr10plus_rejects_new_versions(void)
{
    // libplacebo ignores application versions it does not know either.
    static const int lin[] = {5, 30000, 40, 200, 600, 1500, 4000, 9000, 25000};
    AVBufferRef *buf = hdr10plus(40000, lin);
    ((AVDynamicHDRPlus *)buf->data)->application_version = 2;
    struct ohos_vivid_stats st;
    CHECK(probe_hdr10plus(buf, &st) == OHOS_DYNAMIC_HDR_NONE, "version 2 used");
}

static void test_probe_native_vivid_and_priority(void)
{
    size_t size;
    AVDynamicHDRVivid *vivid = av_dynamic_hdr_vivid_alloc(&size);
    *vivid = full_vivid();
    struct mp_ff_side_data sd[2] = {
        {AV_FRAME_DATA_DYNAMIC_HDR_VIVID,
         av_buffer_create((uint8_t *)vivid, size, NULL, NULL, 0)},
        {AV_FRAME_DATA_DOVI_METADATA, dovi_l1(0, 1000, 3000)},
    };
    struct ohos_vivid_stats st;
    const AVDynamicHDRVivid *native;

    struct mp_image img = image_with(sd, 1);
    CHECK(ohos_vivid_probe(&img, &st, &native) == OHOS_DYNAMIC_HDR_VIVID, "kind");
    CHECK(native == vivid, "native payload");
    CHECK(fabsf(st.var * 4095 - 700) < 1e-3, "native var %f", st.var * 4095);

    img = image_with(sd, 2);
    CHECK(ohos_vivid_probe(&img, &st, &native) == OHOS_DYNAMIC_HDR_DOVI,
          "Dolby Vision wins");
    CHECK(!native, "no native payload with Dolby Vision");

    img = image_with(NULL, 0);
    CHECK(ohos_vivid_probe(&img, &st, &native) == OHOS_DYNAMIC_HDR_NONE, "none");
    av_buffer_unref(&sd[0].buf);
    av_buffer_unref(&sd[1].buf);
}

static struct pl_tone_map_params tone(float in_max, float out_max)
{
    struct pl_tone_map_params t = {
        .function = &pl_tone_map_spline,
        .input_scaling = PL_HDR_PQ,
        .output_scaling = PL_HDR_PQ,
        .input_min = pq(0.005f),
        .input_max = pq(in_max),
        .output_min = pq(0.005f),
        .output_max = pq(out_max),
    };
    pl_tone_map_params_infer(&t);
    return t;
}

static void test_stats_from_static(void)
{
    struct pl_hdr_metadata hdr = {
        .min_luma = 0.005f, .max_luma = 4000, .max_cll = 1500, .max_fall = 400,
    };
    struct ohos_vivid_stats st;
    ohos_vivid_stats_from_static(&st, &hdr);
    CHECK(fabsf(st.max - pq(1500)) < 1e-4, "MaxCLL is the peak: %f", st.max);
    CHECK(fabsf(st.avg - pq(400)) < 1e-4, "MaxFALL is the average: %f", st.avg);
    CHECK(fabsf(st.min - pq(0.005f)) < 1e-4, "min %f", st.min);
    CHECK(st.var > 0, "var %f", st.var);

    // No content light level: the mastering peak and diffuse white stand in.
    hdr = (struct pl_hdr_metadata){.min_luma = 0.0001f, .max_luma = 1000};
    ohos_vivid_stats_from_static(&st, &hdr);
    CHECK(fabsf(st.max - pq(1000)) < 1e-4, "mastering peak: %f", st.max);
    CHECK(fabsf(st.avg - pq(203)) < 1e-4, "diffuse white: %f", st.avg);
}

static void test_tone_map(void)
{
    const struct ohos_vivid_stats src = {
        .min = pq(0.005f), .avg = pq(100), .var = 0.1f, .max = pq(4000),
    };

    struct ohos_vivid_stats st = src;
    struct pl_tone_map_params noop = tone(4000, 4000);
    ohos_vivid_tone_map(&st, &noop);
    CHECK(!memcmp(&st, &src, sizeof(st)), "noop changed the statistics");

    st = src;
    struct pl_tone_map_params down = tone(4000, 1000);
    ohos_vivid_tone_map(&st, &down);
    CHECK(fabsf(st.max - pq(1000)) < 2e-3, "max %f vs %f", st.max, pq(1000));
    CHECK(fabsf(st.min - src.min) < 2e-3, "min %f", st.min);
    CHECK(st.avg <= src.avg + 1e-6 && st.avg > src.min, "avg %f", st.avg);
    CHECK(st.var > 0 && st.var <= src.var + 1e-6, "var %f", st.var);
    CHECK(st.min <= st.avg && st.avg <= st.max, "ordered");
}

static struct pl_color_space bt2020_pq(float max_luma)
{
    return (struct pl_color_space){
        .primaries = PL_COLOR_PRIM_BT_2020,
        .transfer = PL_COLOR_TRC_PQ,
        .hdr = {.min_luma = 0.005f, .max_luma = max_luma},
    };
}

static void test_tone_map_params(void)
{
    struct pl_color_map_params params = pl_color_map_default_params;
    params.tone_mapping_function = &pl_tone_map_spline;
    const struct pl_color_space target = bt2020_pq(1000);
    struct pl_tone_map_params t;

    // A scene dimmer than the target passes through untouched: libplacebo
    // never maps beyond the source.
    struct pl_color_space src = bt2020_pq(1000);
    src.hdr.max_pq_y = pq(400);
    src.hdr.avg_pq_y = pq(60);
    ohos_vivid_tone_map_params(&t, &params, &src, &target);
    CHECK(pl_tone_map_params_noop(&t), "a 400 nit scene is expanded to 1000");

    // A brighter one is compressed into the target.
    src.hdr.max_pq_y = pq(3000);
    ohos_vivid_tone_map_params(&t, &params, &src, &target);
    CHECK(!pl_tone_map_params_noop(&t), "a 3000 nit scene passes through");
    CHECK(fabsf(t.input_max - pq(3000)) < 1e-4, "input max %f", t.input_max);
    CHECK(fabsf(t.output_max - pq(1000)) < 1e-4, "output max %f", t.output_max);

    // Static metadata at the target's own peak: nothing to do.
    ohos_vivid_tone_map_params(&t, &params, &target, &target);
    CHECK(pl_tone_map_params_noop(&t), "equal peaks are mapped");

    // libplacebo skips colour mapping between equal spaces, even when a scene
    // tops the static peak they share.
    ohos_vivid_tone_map_params(&t, &params, &src, &src);
    CHECK(pl_tone_map_params_noop(&t), "a stream is mapped into itself");

    // Only inverse tone mapping expands a dim scene.
    params.inverse_tone_mapping = true;
    src.hdr.max_pq_y = pq(400);
    ohos_vivid_tone_map_params(&t, &params, &src, &target);
    CHECK(!pl_tone_map_params_noop(&t), "inverse tone mapping is lost");

    // No colour mapping parameters: libplacebo's defaults.
    const struct pl_color_space bright = bt2020_pq(4000);
    ohos_vivid_tone_map_params(&t, NULL, &bright, &target);
    CHECK(!pl_tone_map_params_noop(&t) && t.function, "defaults");
}

static AVBufferRef *vivid_buf(void)
{
    size_t size;
    AVDynamicHDRVivid *vivid = av_dynamic_hdr_vivid_alloc(&size);
    *vivid = full_vivid();
    return av_buffer_create((uint8_t *)vivid, size, NULL, NULL, 0);
}

static void test_tracker(void)
{
    struct mp_ff_side_data vivid_sd = {AV_FRAME_DATA_DYNAMIC_HDR_VIVID, vivid_buf()};
    struct mp_ff_side_data dovi_sd = {AV_FRAME_DATA_DOVI_METADATA,
                                      dovi_l1(0, 1000, 3000)};
    struct mp_image vivid = image_with(&vivid_sd, 1);
    struct mp_image dovi = image_with(&dovi_sd, 1);
    struct mp_image plain = image_with(NULL, 0);
    struct ohos_vivid_tracker t = {0};

    ohos_vivid_tracker_update(&t, &vivid, 1);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_VIVID && t.native_size > 0, "vivid");
    size_t native_size = t.native_size;

    // Frames repeating the scene without metadata keep it.
    ohos_vivid_tracker_update(&t, &plain, 2);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_VIVID && t.native_size == native_size,
          "dropped within the stream");

    // After a seek or a new stream, redrawing the frame on screen keeps it...
    ohos_vivid_tracker_reset(&t);
    ohos_vivid_tracker_update(&t, &plain, 2);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_VIVID, "dropped on a redraw");
    // ...but the first new frame does not: plain HDR10 after HDR Vivid.
    ohos_vivid_tracker_update(&t, &plain, 14);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_NONE && !t.native_size,
          "leaked into the next stream");
    ohos_vivid_tracker_update(&t, &plain, 15);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_NONE, "none");

    // A new stream with its own metadata takes over at once.
    ohos_vivid_tracker_update(&t, &vivid, 16);
    ohos_vivid_tracker_reset(&t);
    ohos_vivid_tracker_update(&t, &dovi, 28);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_DOVI && !t.native_size, "dolby vision");
    CHECK(fabsf(t.stats.max * 4095 - 3000) < 1e-3, "max %f", t.stats.max * 4095);

    // The reset applies once only.
    ohos_vivid_tracker_update(&t, &plain, 29);
    CHECK(t.dynamic == OHOS_DYNAMIC_HDR_DOVI, "reset twice");

    av_buffer_unref(&vivid_sd.buf);
    av_buffer_unref(&dovi_sd.buf);
}

int main(void)
{
    test_full_round_trip();
    test_stats_round_trip();
    test_stats_are_ordered();
    test_rejects_unknown_versions();
    test_probe_dovi();
    test_dovi_describe();
    test_probe_hdr10plus();
    test_hdr10plus_dark_scene();
    test_hdr10plus_rejects_new_versions();
    test_probe_native_vivid_and_priority();
    test_stats_from_static();
    test_tone_map();
    test_tone_map_params();
    test_tracker();
    return report();
}
