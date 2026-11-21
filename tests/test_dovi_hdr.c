// Checks video/dovi_hdr.c: the luminance of a shot Dolby Vision levels 1 and 3
// give, as HDR Vivid and libplacebo's tone mapping take it, and the static HDR
// metadata levels 6 and 9 carry, filled into what the stream's own HDR10
// metadata leaves out.
#include <math.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/utils/dolbyvision.h>
#include <libplacebo/utils/libav.h>

#include "common/common.h"
#include "video/dovi_hdr.h"
#include "test_util.h"

// A frame carrying an RPU with the given extension blocks.
static AVFrame *frame_of(const AVDOVIDmData *blocks, int num)
{
    AVFrame *frame = av_frame_alloc();
    av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_METADATA,
                                    dovi_rpu(blocks, num));
    return frame;
}

// The metadata of a frame carrying the given blocks, on top of hdr.
static struct pl_hdr_metadata fill(const AVDOVIDmData *blocks, int num,
                                   struct pl_hdr_metadata hdr)
{
    AVFrame *frame = frame_of(blocks, num);
    mp_dovi_map_hdr(&hdr, frame);
    av_frame_free(&frame);
    return hdr;
}

static const struct pl_hdr_metadata no_hdr = {0};

// dovi_tool's PREDEFINED_COLORSPACE_PRIMARIES, the table level 9 indexes
// (dolby_vision/src/rpu/extension_metadata/primaries.rs): red, green, blue
// and white point x/y.
static const float presets[][8] = {
    {0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329},      // DCI-P3 D65
    {0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.329},       // BT.709
    {0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.329}, // BT.2020
    {0.63, 0.34, 0.31, 0.595, 0.155, 0.07, 0.3127, 0.329},     // SMPTE-C
    {0.64, 0.33, 0.29, 0.60, 0.15, 0.06, 0.3127, 0.329},       // BT.601 PAL
    {0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.314, 0.351},       // DCI-P3
};

static bool prim_is(const struct pl_raw_primaries *p, const float want[8])
{
    const float got[8] = {p->red.x, p->red.y, p->green.x, p->green.y,
                          p->blue.x, p->blue.y, p->white.x, p->white.y};
    for (int i = 0; i < 8; i++) {
        if (fabsf(got[i] - want[i]) > 1e-4)
            return false;
    }
    return true;
}

// Exactly zero: not NaN, not partly filled.
static bool prim_unset(const struct pl_raw_primaries *p)
{
    const struct pl_raw_primaries zero = {0};
    return !memcmp(p, &zero, sizeof(zero));
}

static const AVDOVIDmData l6 = {.level = 6, .l6 = {
    .max_luminance = 1000, .min_luminance = 50, .max_cll = 1680, .max_fall = 400,
}};

static AVDOVIDmData l9(int index)
{
    return (AVDOVIDmData){.level = 9, .l9 = {.source_primary_index = index}};
}

// Level 6 is HDR10's static metadata: whole nits, except for the minimum
// mastering luminance in units of 0.0001 nits.
static void test_level6(void)
{
    const struct {
        AVDOVIDmLevel6 l6;
        float max_luma, min_luma, max_cll, max_fall;
    } cases[] = {
        {l6.l6, 1000, 0.005f, 1680, 400},
        // Light levels beyond what PQ can carry are garbage, not content. Zero
        // means unknown in the RPU too, and the mastering minimum goes with
        // its maximum.
        {{.max_luminance = 20000, .min_luminance = 1, .max_cll = 65535},
         0, 0, 0, 0},
        // PQ reaches 10000 nits exactly.
        {{.max_luminance = 10000, .min_luminance = 50,
          .max_cll = 10000, .max_fall = 10000}, 10000, 0.005f, 10000, 10000},
        {{.max_luminance = 10001, .min_luminance = 50,
          .max_cll = 10001, .max_fall = 10001}, 0, 0, 0, 0},
        // The mastering luminance is checked as libplacebo checks the SEI's: a
        // peak under 5 nits, or a black at or above the peak, is no mastering
        // display.
        {{.max_luminance = 3, .min_luminance = 1, .max_cll = 1000},
         0, 0, 1000, 0},
        {{.max_luminance = 5, .min_luminance = 60000}, 0, 0, 0, 0},
    };
    for (int i = 0; i < MP_ARRAY_SIZE(cases); i++) {
        const AVDOVIDmData block = {.level = 6, .l6 = cases[i].l6};
        struct pl_hdr_metadata hdr = fill(&block, 1, no_hdr);
        CHECK(hdr.max_luma == cases[i].max_luma &&
              fabsf(hdr.min_luma - cases[i].min_luma) < 1e-7 &&
              hdr.max_cll == cases[i].max_cll &&
              hdr.max_fall == cases[i].max_fall && prim_unset(&hdr.prim),
              "case %d: max_luma %f min_luma %f max_cll %f max_fall %f", i,
              hdr.max_luma, hdr.min_luma, hdr.max_cll, hdr.max_fall);
    }
}

// What the stream's own metadata says stays; the RPU only fills gaps.
static void test_keeps_stream_metadata(void)
{
    const AVDOVIDmData blocks[] = {l6, l9(0)};
    const struct pl_hdr_metadata sei = {
        .prim = *pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020),
        .min_luma = 0.0001f,
        .max_luma = 4000,
        .max_cll = 2000,
        // HDR10+ with one channel's peak at zero
        .scene_max = {1000, 0, 2000},
        .scene_avg = 100,
    };
    struct pl_hdr_metadata hdr = fill(blocks, 2, sei);
    CHECK(pl_raw_primaries_equal(&hdr.prim, &sei.prim), "primaries replaced");
    CHECK(hdr.min_luma == sei.min_luma && hdr.max_luma == sei.max_luma,
          "mastering luminance %f-%f", hdr.min_luma, hdr.max_luma);
    CHECK(hdr.max_cll == sei.max_cll, "max_cll %f", hdr.max_cll);
    CHECK(hdr.max_fall == 400, "max_fall %f", hdr.max_fall);
    CHECK(!memcmp(hdr.scene_max, sei.scene_max, sizeof(sei.scene_max)) &&
          hdr.scene_avg == sei.scene_avg,
          "HDR10+ scene %f %f %f avg %f", hdr.scene_max[0], hdr.scene_max[1],
          hdr.scene_max[2], hdr.scene_avg);

    struct pl_hdr_metadata with_fall = sei;
    with_fall.max_fall = 300;
    hdr = fill(blocks, 2, with_fall);
    CHECK(hdr.max_fall == 300, "max_fall %f", hdr.max_fall);
}

// Primaries go in whole or not at all: a valid gamut of the stream's stays as
// it is even with a zero coordinate, one it only partly gave is replaced.
static void test_level9_all_or_nothing(void)
{
    const AVDOVIDmData block = l9(1);
    const struct pl_hdr_metadata aces = {
        .prim = *pl_raw_primaries_get(PL_COLOR_PRIM_ACES_AP0),
    };
    struct pl_hdr_metadata hdr = fill(&block, 1, aces);
    CHECK(pl_raw_primaries_equal(&hdr.prim, &aces.prim), "valid gamut changed");

    const struct pl_hdr_metadata partial = {.prim.red = {0.68, 0.32}};
    hdr = fill(&block, 1, partial);
    CHECK(prim_is(&hdr.prim, presets[1]), "partial gamut not replaced");
}

static void test_level9_presets(void)
{
    for (int i = 0; i < 6; i++) {
        const AVDOVIDmData block = l9(i);
        struct pl_hdr_metadata hdr = fill(&block, 1, no_hdr);
        CHECK(prim_is(&hdr.prim, presets[i]), "preset %d", i);
    }

    // The rest of dovi_tool's table are camera gamuts (ACES, S-Gamut), which
    // no mastering display has; other indices are undefined.
    const int unknown[] = {6, 7, 8, 9, 254};
    for (int i = 0; i < 5; i++) {
        const AVDOVIDmData block = l9(unknown[i]);
        struct pl_hdr_metadata hdr = fill(&block, 1, no_hdr);
        CHECK(prim_unset(&hdr.prim), "index %d", unknown[i]);
    }
}

static AVCIExy cie_xy(float x, float y)
{
    return (AVCIExy){av_make_q(lrintf(x * 32767), 32767),
                     av_make_q(lrintf(y * 32767), 32767)};
}

// Primaries as FFmpeg reads them from the block: signed 16 bit over 32767.
static AVColorPrimariesDesc primaries_desc(const float p[8])
{
    return (AVColorPrimariesDesc){
        .wp = cie_xy(p[6], p[7]),
        .prim = {
            .r = cie_xy(p[0], p[1]),
            .g = cie_xy(p[2], p[3]),
            .b = cie_xy(p[4], p[5]),
        },
    };
}

// Index 255 carries the primaries in the block itself; a block too short for
// them leaves them zero.
static void test_level9_custom(void)
{
    const float *p3 = presets[0];
    AVDOVIDmData block = l9(255);
    block.l9.source_display_primaries = primaries_desc(p3);
    struct pl_hdr_metadata hdr = fill(&block, 1, no_hdr);
    CHECK(prim_is(&hdr.prim, p3), "custom primaries");

    block = l9(255);
    hdr = fill(&block, 1, no_hdr);
    CHECK(prim_unset(&hdr.prim), "short block");

    // Each primary in turn moved outside the chromaticity diagram, where no
    // display has one, in a gamut pl_primaries_valid() would still accept.
    const float outside[3][2] = {{0.75, 0.30}, {0.265, 0.80}, {0.15, -0.05}};
    for (int i = 0; i < 3; i++) {
        float p[8];
        memcpy(p, p3, sizeof(p));
        p[2 * i] = outside[i][0];
        p[2 * i + 1] = outside[i][1];
        block.l9.source_display_primaries = primaries_desc(p);
        hdr = fill(&block, 1, no_hdr);
        CHECK(prim_unset(&hdr.prim), "primary %d outside", i);
    }
}

// An RPU without levels 6 and 9 leaves the metadata as it was.
static void test_nothing_to_fill(void)
{
    const AVDOVIDmData block = l1(0, 0, 3079);
    struct pl_hdr_metadata hdr = fill(&block, 1, no_hdr);
    CHECK(pl_hdr_metadata_equal(&hdr, &no_hdr), "metadata invented");
}

static bool shot_of(const AVDOVIDmData *blocks, int num,
                    struct mp_dovi_shot *shot)
{
    AVBufferRef *rpu = dovi_rpu(blocks, num);
    bool ok = mp_dovi_shot((const AVDOVIMetadata *)rpu->data, shot);
    av_buffer_unref(&rpu);
    return ok;
}

// A shot is level 1, the CM v2.9 analysis, and CM v4.0 reconstructs its RGB
// based one as level 1 plus level 3. The result is kept in order within the PQ
// range, its peak never below what level 1 measured (an RGB based peak is not
// below the CM v2.9 one, and a lower one would clip the highlights) and its
// average off black.
static void test_shot(void)
{
    const struct {
        AVDOVIDmData l1, l3;    // level 3 only where its level is not 0
        float min, avg, max;
    } cases[] = {
        {l1(100, 1000, 3000), {0}, code(100), code(1000), code(3000)},
        {l1(100, 1000, 3000), l3(20, 300, 100),
         code(100) + offset(20), code(1000) + offset(300),
         code(3000) + offset(100)},
        {l1(100, 1000, 3000), l3(0, 0, -300), code(100), code(1000), code(3000)},
        // Offsets past one another: the average under the minimum, then over
        // the peak, and the minimum over the peak...
        {l1(100, 1000, 3000), l3(0, -2000, 0), code(100), code(100), code(3000)},
        {l1(100, 1000, 3000), l3(0, 2000, 0), code(100), code(3000), code(3000)},
        {l1(100, 1000, 3000), l3(2000, 0, 0),
         code(3000), code(3000), code(3000)},
        // ...and past either end of the PQ range.
        {l1(100, 1000, 3000), l3(-2048, 0, 2047), 0, code(1000), 1},
        // A shot with a peak is not black on average: its average stays at the
        // lowest code at least, whether level 1 rounded it to black or a mid
        // tone offset took it there.
        {l1(0, 0, 3000), {0}, 0, code(1), code(3000)},
        {l1(0, 1000, 3000), l3(0, -2047, 0), 0, code(1), code(3000)},
        // Three shots of a Dolby Vision test stream logged on a device: one
        // picture (level 1 0/1706/3344) with level 3 mid offsets coded 1195,
        // 1886 and 2910. The lowest reaches black and the highest passes the
        // peak; HDR Vivid got the averages 0, 1382 and 3344 before the average
        // was kept off black.
        {l1(0, 1706, 3344), l3(0, 1195 - 2048, 0), 0, code(1), code(3344)},
        {l1(0, 1706, 3344), l3(0, 1886 - 2048, 0),
         0, code(1706) + offset(1886 - 2048), code(3344)},
        {l1(0, 1706, 3344), l3(0, 2910 - 2048, 0), 0, code(3344), code(3344)},
    };
    for (int i = 0; i < MP_ARRAY_SIZE(cases); i++) {
        const AVDOVIDmData blocks[] = {cases[i].l1, cases[i].l3};
        struct mp_dovi_shot shot = {0};
        CHECK(shot_of(blocks, cases[i].l3.level ? 2 : 1, &shot) &&
              near(shot.min, cases[i].min) && near(shot.avg, cases[i].avg) &&
              near(shot.max, cases[i].max),
              "case %d: min %f avg %f max %f", i, shot.min, shot.avg, shot.max);
    }
    // The device's middle shot, as the code HDR Vivid takes.
    const AVDOVIDmData middle[] = {l1(0, 1706, 3344), l3(0, 1886 - 2048, 0)};
    struct mp_dovi_shot shot = {0};
    CHECK(shot_of(middle, 2, &shot) && lrintf(shot.avg * 4095) == 1382,
          "avg code %f", shot.avg * 4095);
}

// Level 3 may come before level 1.
static void test_shot_block_order(void)
{
    const AVDOVIDmData blocks[] = {l3(20, 300, 100), l1(100, 1000, 3000)};
    struct mp_dovi_shot shot = {0};
    CHECK(shot_of(blocks, 2, &shot) &&
          near(shot.min, code(100) + offset(20)) &&
          near(shot.avg, code(1000) + offset(300)) &&
          near(shot.max, code(3000) + offset(100)),
          "min %f avg %f max %f", shot.min, shot.avg, shot.max);
}

// Without a level 1 peak there is no shot to describe, and level 3 alone
// offsets nothing.
static void test_shot_needs_level1(void)
{
    struct mp_dovi_shot shot;
    CHECK(!shot_of(NULL, 0, &shot), "empty RPU");
    const AVDOVIDmData only_l3[] = {l3(20, 300, 100)};
    CHECK(!shot_of(only_l3, 1, &shot), "level 3 alone");
    const AVDOVIDmData no_peak[] = {l1(0, 0, 0), l3(20, 300, 100)};
    CHECK(!shot_of(no_peak, 2, &shot), "level 1 without a peak");
}

// libplacebo's libdovi path, which the host build leaves out. Like libdovi it
// maps level 1 alone from the raw RPU, of a stream with an enhancement layer
// too; the raw RPU here is just that level 1 block.
void pl_hdr_metadata_from_dovi_rpu(struct pl_hdr_metadata *out,
                                   const uint8_t *buf, size_t size)
{
    AVDOVIDmLevel1 level1;
    if (size != sizeof(level1))
        return;
    memcpy(&level1, buf, sizeof(level1));
    out->max_pq_y = level1.max_pq / 4095.0f;
    out->avg_pq_y = level1.avg_pq / 4095.0f;
}

// How map_frame() treats a frame.
enum {
    DUAL_LAYER = 1 << 0,    // an enhancement layer stream (no residual flag)
    LIBDOVI    = 1 << 1,    // the raw RPU is there for libdovi
    PLAIN      = 1 << 2,    // libplacebo's own mapping only
};

// A Dolby Vision frame as mp_image.c maps it. libplacebo maps the RPU of a
// single layer stream itself, the source display range and level 1; then
// mp_dovi_map_hdr() runs, with the raw RPU if there is one.
static struct pl_color_space map_frame(const AVDOVIDmData *blocks, int num,
                                       int flags)
{
    AVFrame *frame = frame_of(blocks, num);
    AVDOVIMetadata *md = (AVDOVIMetadata *)
        av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA)->data;
    av_dovi_get_header(md)->disable_residual_flag = !(flags & DUAL_LAYER);
    av_dovi_get_color(md)->source_min_pq = 62;
    av_dovi_get_color(md)->source_max_pq = 3696;
    struct pl_color_space color = {0};
    struct pl_color_repr repr = {0};
    struct pl_dovi_metadata dovi;
    pl_map_avdovi_metadata(&color, &repr, &dovi, md);
    if (!(flags & PLAIN)) {
        const AVDOVIDmData *level1 = av_dovi_find_level(md, 1);
        if ((flags & LIBDOVI) && level1) {
            AVBufferRef *raw = av_buffer_alloc(sizeof(level1->l1));
            memcpy(raw->data, &level1->l1, sizeof(level1->l1));
            av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER,
                                            raw);
        }
        mp_dovi_map_hdr(&color.hdr, frame);
    }
    av_frame_free(&frame);
    return color;
}

// The peak and average libplacebo tone maps a picture by.
static void tone_map_input(const struct pl_color_space *color, float *max,
                           float *avg)
{
    pl_color_space_nominal_luma_ex(pl_nominal_luma_params(
        .color    = color,
        .metadata = PL_HDR_METADATA_ANY,
        .scaling  = PL_HDR_PQ,
        .out_max  = max,
        .out_avg  = avg,
    ));
}

// libplacebo maps level 1 alone; level 3 completes it and leaves the rest of
// what libplacebo mapped as it was.
static void test_map_level3(void)
{
    const AVDOVIDmData blocks[] = {l1(100, 1000, 3000), l3(20, 300, 100)};
    struct pl_color_space plain = map_frame(blocks, 2, PLAIN);
    CHECK(near(plain.hdr.max_pq_y, code(3000)) &&
          near(plain.hdr.avg_pq_y, code(1000)),
          "libplacebo's own mapping: max %f avg %f",
          plain.hdr.max_pq_y, plain.hdr.avg_pq_y);

    struct pl_color_space color = map_frame(blocks, 2, 0);
    float max = 0, avg = 0;
    tone_map_input(&color, &max, &avg);
    CHECK(near(max, code(3000) + offset(100)) &&
          near(avg, code(1000) + offset(300)),
          "tone mapped by max %f avg %f", max, avg);

    struct pl_hdr_metadata rest = color.hdr;
    rest.max_pq_y = plain.hdr.max_pq_y;
    rest.avg_pq_y = plain.hdr.avg_pq_y;
    CHECK(pl_hdr_metadata_equal(&rest, &plain.hdr), "other metadata changed");

    // Without level 3 the frame keeps level 1 as libplacebo mapped it.
    color = map_frame(blocks, 1, 0);
    CHECK(near(color.hdr.max_pq_y, code(3000)) &&
          near(color.hdr.avg_pq_y, code(1000)),
          "level 1 only: max %f avg %f", color.hdr.max_pq_y,
          color.hdr.avg_pq_y);
}

// libplacebo reads a zero average as no dynamic metadata at all and would tone
// map by the static peak (here 4000 nits) instead. A mid tone offset that
// reaches black is the darkest average there is, and so is a black level 1.
static void test_map_level3_black_average(void)
{
    const AVDOVIDmData trimmed[] = {l1(0, 1706, 3344), l3(0, 1195 - 2048, 0)};
    const AVDOVIDmData black[] = {l1(0, 0, 3344)};
    const struct {
        const AVDOVIDmData *blocks;
        int num;
    } cases[] = {{trimmed, 2}, {black, 1}};
    for (int i = 0; i < MP_ARRAY_SIZE(cases); i++) {
        struct pl_color_space color = map_frame(cases[i].blocks, cases[i].num,
                                                0);
        float max = 0, avg = 0;
        tone_map_input(&color, &max, &avg);
        CHECK(near(max, code(3344)) && near(avg, code(1)),
              "case %d: tone mapped by max %f avg %f", i, max, avg);
    }
}

// libdovi maps level 1 once more from the raw RPU, and also for a stream with
// an enhancement layer, which libplacebo does not map otherwise: level 3
// still completes it either way.
static void test_map_after_libdovi(void)
{
    const AVDOVIDmData blocks[] = {l1(100, 1000, 3000), l3(20, 300, 100)};
    const int flags[] = {LIBDOVI, LIBDOVI | DUAL_LAYER};
    for (int i = 0; i < MP_ARRAY_SIZE(flags); i++) {
        struct pl_color_space color = map_frame(blocks, 2, flags[i]);
        CHECK(near(color.hdr.max_pq_y, code(3000) + offset(100)) &&
              near(color.hdr.avg_pq_y, code(1000) + offset(300)),
              "flags %d: max %f avg %f", flags[i], color.hdr.max_pq_y,
              color.hdr.avg_pq_y);
    }
}

// Frames libplacebo took no level 1 from stay as they are (an enhancement
// layer stream without libdovi), as do frames whose RPU has no level 1.
static void test_map_unmapped(void)
{
    const AVDOVIDmData blocks[] = {l1(100, 1000, 3000), l3(20, 300, 100)};
    struct pl_color_space color = map_frame(blocks, 2, DUAL_LAYER);
    CHECK(!color.hdr.max_pq_y && !color.hdr.avg_pq_y,
          "CIE_Y invented: max %f avg %f", color.hdr.max_pq_y,
          color.hdr.avg_pq_y);

    const AVDOVIDmData only_l3[] = {l3(20, 300, 100)};
    const struct pl_hdr_metadata mapped = {
        .max_pq_y = code(3000), .avg_pq_y = code(1000),
    };
    struct pl_hdr_metadata hdr = fill(only_l3, 1, mapped);
    CHECK(pl_hdr_metadata_equal(&hdr, &mapped), "max %f avg %f",
          hdr.max_pq_y, hdr.avg_pq_y);
}

int main(void)
{
    test_level6();
    test_keeps_stream_metadata();
    test_level9_all_or_nothing();
    test_level9_presets();
    test_level9_custom();
    test_nothing_to_fill();
    test_shot();
    test_shot_block_order();
    test_shot_needs_level1();
    test_map_level3();
    test_map_level3_black_average();
    test_map_after_libdovi();
    test_map_unmapped();
    return report();
}
