#!/bin/bash

set -eu

# ffmpeg (dash demuxer 依赖 libxml2, 必须先构建; libwebp 提供 webp 编码)
./scripts/libxml2.sh build
./scripts/mbedtls.sh build
./scripts/dav1d.sh build
./scripts/libwebp.sh build
./scripts/ffmpeg.sh build

# libass
./scripts/fribidi.sh build
./scripts/freetype.sh build
./scripts/harfbuzz.sh build
./scripts/fontconfig.sh build
./scripts/libass.sh build

# libplacebo
./scripts/dovi_tools.sh build
./scripts/lcms.sh build
./scripts/shaderc.sh build
./scripts/libplacebo.sh build

# mpv
./scripts/lua.sh build
./scripts/mpv.sh build
