#!/bin/bash
# Host-side tests for the HarmonyOS HDR code of the mpv fork.
#
# Builds minimal host copies of FFmpeg and libplacebo out of tree from the
# sources ./download.sh fetched, then the tests against them:
#  - test_vivid: the HDR Vivid conversion, its serializer checked against
#    FFmpeg's own parser;
#  - test_dovi_hdr: video/dovi_hdr.c, the luminance of Dolby Vision levels 1
#    and 3 as libplacebo's own mapping takes it, and the static HDR metadata
#    of levels 6 and 9;
#  - test_ohos_window: video/out/ohos_common.c with the NativeWindow stubbed
#    out. It needs the configuration of a finished libmpv build (bundle.sh)
#    and the OHOS SDK headers.

set -eu

ROOT_DIR=$(cd "$(dirname "$0")/.."; pwd)
SRC=$ROOT_DIR/libmpv
OUT=$ROOT_DIR/tests/.host
MPV=$SRC/mpv
SYSROOT=${OHOS_NDK_HOME:-/sdk/linux}/native/sysroot/usr/include

if [ ! -f "$MPV/.build/config.h" ] || [ ! -d "$SYSROOT" ]; then
  echo "Missing $MPV/.build/config.h or $SYSROOT: build libmpv first." >&2
  exit 1
fi

mkdir -p "$OUT/ffmpeg"

if [ ! -f "$OUT/ffmpeg/libavcodec/libavcodec.a" ]; then
  echo "Building host FFmpeg..."
  (cd "$OUT/ffmpeg" &&
    "$SRC/ffmpeg/configure" --disable-everything --disable-programs \
      --disable-doc --disable-avformat --disable-avfilter --disable-avdevice \
      --disable-swresample --disable-swscale --disable-autodetect \
      --disable-x86asm --enable-decoder=hevc > /dev/null &&
    make -j"$(nproc)" libavutil/libavutil.a libavcodec/libavcodec.a > /dev/null)
fi

# dovi: test_dovi_hdr runs libplacebo's own Dolby Vision mapping, so a tree
# set up without it is set up afresh.
if [ ! -f "$OUT/placebo/src/libplacebo.a" ] || ! grep -qs "define PL_HAVE_DOVI 1" \
     "$OUT/placebo/src/include/libplacebo/config.h"; then
  echo "Building host libplacebo..."
  rm -rf "$OUT/placebo"
  meson setup "$OUT/placebo" "$SRC/libplacebo" --default-library=static \
    -Dvulkan=disabled -Dopengl=disabled -Dd3d11=disabled \
    -Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled \
    -Ddovi=enabled -Dlibdovi=disabled -Dunwind=disabled \
    -Dxxhash=disabled -Ddemos=false -Dtests=false > /dev/null
  ninja -C "$OUT/placebo" > /dev/null
fi

CFLAGS=(-O1 -g -std=gnu11 -Wall -Werror)
INCLUDES=(-I"$MPV" -I"$SRC/ffmpeg" -I"$OUT/ffmpeg"
          -I"$SRC/libplacebo/src/include" -I"$OUT/placebo/src/include")
LIBS=("$OUT/ffmpeg/libavcodec/libavcodec.a" "$OUT/ffmpeg/libavutil/libavutil.a"
      "$OUT/placebo/src/libplacebo.a" -lm -lpthread -lstdc++)

cc "${CFLAGS[@]}" -o "$OUT/test_vivid" \
  "$ROOT_DIR/tests/test_vivid.c" "$MPV/video/out/ohos_hdr_vivid.c" \
  "$MPV/video/dovi_hdr.c" "${INCLUDES[@]}" "${LIBS[@]}"

cc "${CFLAGS[@]}" -o "$OUT/test_dovi_hdr" \
  "$ROOT_DIR/tests/test_dovi_hdr.c" "$MPV/video/dovi_hdr.c" \
  "${INCLUDES[@]}" "${LIBS[@]}"

# GCC does not know the clang availability attributes of the SDK headers.
cc "${CFLAGS[@]}" -D_GNU_SOURCE '-D__availability__(...)=' \
  -o "$OUT/test_ohos_window" \
  "$ROOT_DIR/tests/test_ohos_window.c" "$MPV/video/out/ohos_hdr_vivid.c" \
  "$MPV/video/dovi_hdr.c" "$MPV/ta/ta.c" "$MPV/ta/ta_talloc.c" \
  "$MPV/ta/ta_utils.c" \
  "${INCLUDES[@]}" -I"$MPV/.build" -idirafter "$SYSROOT" "${LIBS[@]}"

status=0
"$OUT/test_vivid" || status=1
"$OUT/test_dovi_hdr" || status=1
"$OUT/test_ohos_window" || status=1
exit $status
