// Drives video/dovi_tone_map.c through libplacebo's own tone mapping entry
// points, on the shots of a Dolby Vision trim test: one picture graded with
// level 3 mid offsets of 0 and +-0.5.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <libplacebo/colorspace.h>
#include <libplacebo/tone_mapping.h>

#include "common/common.h"
#include "video/dovi_tone_map.h"

#include "test_util.h"

static float pq(float nits)
{
    return pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits);
}

static float nits(float pq)
{
    return pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, pq);
}

// The test picture's level 1 (minimum 0, average 1706, maximum 3344) with the
// level 3 mid offsets of its shots, combined as mp_dovi_shot() does: the
// average is kept between one code above black and the peak, which is where
// the encoder left the -0.5 shot.
enum shot { MINUS, BASE, PLUS };

static float shot_avg(enum shot s)
{
    static const int l3_avg[] = {1195, 1886, 2910};
    float avg = 1706 / 4095.0f + (l3_avg[s] - 2048) / 2048.0f;
    return MPCLAMP(avg, 1 / 4095.0f, 3344 / 4095.0f);
}

// What libplacebo hands a curve for a PQ stream and a PQ target: both ranges
// down to absolute black, the shot's peak and average from its metadata.
static struct pl_tone_map_params shot_params(enum shot s, float target)
{
    return (struct pl_tone_map_params){
        .function = &mp_dovi_tone_map,
        .input_scaling = PL_HDR_PQ,
        .output_scaling = PL_HDR_PQ,
        .lut_size = 256,
        .input_min = pq(PL_COLOR_HDR_BLACK),
        .input_max = 3344 / 4095.0f,
        .input_avg = shot_avg(s),
        .output_min = pq(PL_COLOR_HDR_BLACK),
        .output_max = pq(target),
    };
}

static float map(float in, const struct pl_tone_map_params *p)
{
    return nits(pl_tone_map_sample(pq(in), p));
}

static const float mids[] = {1, 5, 16, 39, 100};

// Without a trim the picture keeps its mid tones, down to the target's own
// black; only the highlights are compressed into the target.
static void test_base(void)
{
    for (int t = 0; t < 2; t++) {
        float target = t ? 1600 : 1000;
        struct pl_tone_map_params p = shot_params(BASE, target);
        for (int i = 0; i < MP_ARRAY_SIZE(mids); i++) {
            float out = map(mids[i], &p);
            CHECK(fabsf(out / mids[i] - 1) < 0.03, "%.0f nit target: %.1f nit "
                  "-> %.2f", target, mids[i], out);
        }
        CHECK(map(1000, &p) < 0.95f * fminf(1000, target), "%.0f nit target: "
              "highlights uncompressed (1000 nit -> %.0f)", target,
              map(1000, &p));
        CHECK(pl_tone_map_sample(p.input_min, &p) <= p.output_min + 1e-6f,
              "%.0f nit target: black lifted to %g nit", target,
              nits(pl_tone_map_sample(p.input_min, &p)));
    }
}

// A positive mid offset tells the display the shot is brighter than it is:
// its mid tones come out distinctly darker.
static void test_positive_offset_darkens(void)
{
    struct pl_tone_map_params base = shot_params(BASE, 1000);
    struct pl_tone_map_params plus = shot_params(PLUS, 1000);
    for (int i = 0; i < MP_ARRAY_SIZE(mids); i++) {
        CHECK(map(mids[i], &plus) < 0.75f * map(mids[i], &base),
              "%.0f nit: %.2f vs %.2f", mids[i], map(mids[i], &plus),
              map(mids[i], &base));
    }
    // Brighter targets leave less to compress, and so less to darken.
    base = shot_params(BASE, 1600);
    plus = shot_params(PLUS, 1600);
    CHECK(map(39, &plus) < 0.95f * map(39, &base), "1600 nit target: %.2f vs "
          "%.2f", map(39, &plus), map(39, &base));
}

// A negative one tells it the shot is darker: the display keeps the shot's
// mid above its black, which lifts the shadows and the black itself, the
// washed out look Dolby Vision displays give such a shot.
static void test_negative_offset_lifts(void)
{
    for (int t = 0; t < 2; t++) {
        float target = t ? 1600 : 1000;
        struct pl_tone_map_params base = shot_params(BASE, target);
        struct pl_tone_map_params minus = shot_params(MINUS, target);
        for (int i = 0; i < 3; i++) {
            CHECK(map(mids[i], &minus) > 1.1f * map(mids[i], &base),
                  "%.0f nit target, %.0f nit: %.2f vs %.2f", target, mids[i],
                  map(mids[i], &minus), map(mids[i], &base));
        }
        // To the black of Dolby's target displays, 0.005 cd/m2.
        float black = nits(pl_tone_map_sample(minus.input_min, &minus));
        CHECK(fabsf(black - 0.005f) < 0.0005f, "%.0f nit target: black %g",
              target, black);
    }
}

// A target whose black is above the stream's (an SDR display, say) gets the
// shot's black, with its shadows compressed above it rather than clipped.
static void test_raised_black(void)
{
    struct pl_tone_map_params p = shot_params(BASE, 100);
    p.output_min = pq(0.1f);
    float black = nits(pl_tone_map_sample(p.input_min, &p));
    float a = map(0.05f, &p), b = map(0.2f, &p);
    CHECK(fabsf(black - 0.1f) < 1e-4f, "black %g", black);
    CHECK(a > 0.101f && b > a, "shadows clipped: %g, %g", a, b);
}

// Whatever the shot and target, the curve never decreases, stays within the
// target's range and is finite: its Hermite slopes keep within the
// Fritsch-Carlson bounds.
static void test_monotonic(void)
{
    srand(1);
    int bad = 0;
    char first[160] = "";
    for (int n = 0; n < 20000; n++) {
        float r[4];
        for (int i = 0; i < 4; i++)
            r[i] = rand() / (float)RAND_MAX;
        struct pl_tone_map_params p = shot_params(BASE, 1000);
        p.input_max = 0.3f + 0.7f * r[0];
        p.input_avg = p.input_min + (p.input_max - p.input_min) * r[1];
        p.output_max = 0.2f + (p.input_max - 0.2f) * r[2];
        // SDR targets keep a black above the stream's.
        if (r[3] < 0.3f)
            p.output_min = pq(0.05f + 0.2f * r[3]);
        float lut[256];
        pl_tone_map_generate(lut, &p);
        for (int i = 0; i < 256; i++) {
            bool ok = isfinite(lut[i]) && lut[i] >= p.output_min - 1e-6f &&
                      lut[i] <= p.output_max + 1e-6f &&
                      (!i || lut[i] >= lut[i - 1] - 1e-6f);
            if (!ok && !bad++) {
                snprintf(first, sizeof(first), "max %f avg %f out %f-%f, "
                         "lut[%d] = %f", p.input_max, p.input_avg,
                         p.output_min, p.output_max, i, lut[i]);
            }
        }
    }
    CHECK(!bad, "%d bad values, first: %s", bad, first);
}

// Where Table 1 of US 10,600,166 B2 places the mid, worked out from the
// patent for each way it can go; the curve passes through it.
static void test_mid_placement(void)
{
    const float black = pq(PL_COLOR_HDR_BLACK), peak = 3344 / 4095.0f;
    const struct {
        const char *name;
        float avg, max, out_min, out_max, mid;
    } cases[] = {
        // With room around it, the mid stays.
        {"base shot", shot_avg(BASE), peak, black, pq(1000), shot_avg(BASE)},
        // Highlights that do not fit above it keep half of their range...
        {"bright shot", 0.7f, peak, black, pq(1000), 0.693524f},
        // ...as long as the mid stays above the middle of the target.
        {"SDR target", 0.3f, peak, black, pq(100), 0.261577f},
        // Shadows under Dolby's black keep half of their range above it...
        {"-0.5 shot", shot_avg(MINUS), peak, black, pq(1000), 0.015139f},
        // ...as long as the mid stays below the middle of the target.
        {"raised black", 0.12f, 0.8f, 0.1f, 0.2f, 0.15f},
    };
    for (int i = 0; i < MP_ARRAY_SIZE(cases); i++) {
        struct pl_tone_map_params p = shot_params(BASE, 1000);
        p.input_avg = cases[i].avg;
        p.input_max = cases[i].max;
        p.output_min = cases[i].out_min;
        p.output_max = cases[i].out_max;
        float mid = pl_tone_map_sample(cases[i].avg, &p);
        CHECK(fabsf(mid - cases[i].mid) < 2e-5f, "%s: mid %f, Table 1 %f",
              cases[i].name, mid, cases[i].mid);
    }
}

// An average at black or at the peak leaves one half of the curve empty, one
// a hair off them a half too narrow to evaluate, and one outside the shot's
// range nothing to place.
static void test_degenerate(void)
{
    struct pl_tone_map_params p = shot_params(BASE, 1000);
    const float avgs[] = {p.input_min, p.input_min + 1e-7f, p.input_max - 1e-7f,
                          p.input_max, p.input_min / 2, p.input_max + 0.01f};
    for (int i = 0; i < MP_ARRAY_SIZE(avgs); i++) {
        p.input_avg = avgs[i];
        float lut[256];
        pl_tone_map_generate(lut, &p);
        bool ok = true;
        for (int j = 0; j < 256; j++) {
            ok &= isfinite(lut[j]) && lut[j] >= p.output_min - 1e-6f &&
                  lut[j] <= p.output_max + 1e-6f &&
                  (!j || lut[j] >= lut[j - 1] - 1e-6f);
        }
        // Inside the narrow halves, which the LUT steps over.
        const float xs[] = {p.input_min + 5e-8f, p.input_max - 5e-8f};
        for (int j = 0; j < MP_ARRAY_SIZE(xs); j++) {
            float y = pl_tone_map_sample(xs[j], &p);
            ok &= isfinite(y) && y >= p.output_min && y <= p.output_max;
        }
        CHECK(ok, "average %g", avgs[i]);
    }
}

// Without an average (no level 1) there is no shot to map by: the curve is
// libplacebo's spline, as for any other stream.
static void test_no_average(void)
{
    struct pl_tone_map_params p = shot_params(BASE, 1000);
    p.input_avg = 0;
    struct pl_tone_map_params spline = p;
    spline.function = &pl_tone_map_spline;
    for (int i = 0; i < MP_ARRAY_SIZE(mids); i++) {
        CHECK(map(mids[i], &p) == map(mids[i], &spline), "%.0f nit: %f vs %f",
              mids[i], map(mids[i], &p), map(mids[i], &spline));
    }
}

int main(void)
{
    test_base();
    test_positive_offset_darkens();
    test_negative_offset_lifts();
    test_mid_placement();
    test_raised_black();
    test_monotonic();
    test_degenerate();
    test_no_average();
    return report();
}
