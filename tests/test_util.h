// Shared by the host tests: the CHECK() tally, Dolby Vision RPU builders and
// the one mpv service ohos_hdr_vivid.c relies on.
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <libavutil/buffer.h>
#include <libavutil/dovi_meta.h>

static int failures, checks;

// A copy of common/common.c's, which pulls in far more than the tests need.
int mp_snprintf_cat(char *str, size_t size, const char *format, ...)
{
    size_t len = strnlen(str, size);
    va_list ap;
    va_start(ap, format);
    int r = vsnprintf(str + len, size - len, format, ap);
    va_end(ap);
    return r;
}

#define CHECK(cond, ...) do {                                   \
    checks++;                                                   \
    if (!(cond)) {                                              \
        failures++;                                             \
        printf("FAIL %s:%d: ", __func__, __LINE__);             \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
    }                                                           \
} while (0)

// Prints the tally and returns the exit status.
static inline int report(void)
{
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

// An RPU carrying the given extension blocks, as FFmpeg exports it.
static inline AVBufferRef *dovi_rpu(const AVDOVIDmData *blocks, int num)
{
    size_t size;
    AVDOVIMetadata *md = av_dovi_metadata_alloc(&size);
    md->num_ext_blocks = num;
    for (int i = 0; i < num; i++)
        *av_dovi_get_ext(md, i) = blocks[i];
    return av_buffer_create((uint8_t *)md, size, NULL, NULL, 0);
}

// Level 1 holds 12 bit PQ codes; level 3 offsets them in steps of 1/2048 of
// the PQ range, coded around 2048. l3() takes the signed offsets.
static inline AVDOVIDmData l1(int min, int avg, int max)
{
    return (AVDOVIDmData){.level = 1, .l1 = {
        .min_pq = min, .avg_pq = avg, .max_pq = max,
    }};
}

static inline AVDOVIDmData l3(int min, int avg, int max)
{
    return (AVDOVIDmData){.level = 3, .l3 = {
        .min_pq_offset = 2048 + min, .avg_pq_offset = 2048 + avg,
        .max_pq_offset = 2048 + max,
    }};
}

// A level 1 code and a level 3 offset in PQ units.
static inline float code(int c)
{
    return c / 4095.0f;
}

static inline float offset(int c)
{
    return c / 2048.0f;
}

static inline bool near(float a, float b)
{
    return fabsf(a - b) < 1e-6;
}
