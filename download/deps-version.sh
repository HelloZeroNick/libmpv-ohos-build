#!/bin/bash

set -eu

# HarmonyOS command line tools version (API 26, 含 BiSheng 编译器)
V_SDK=26.0.0.621

# FFmpeg
V_MBEDTLS=3.6.4
V_DAV1D=1.5.1

# libwebp (mpv 动态截图 webp 编码)
V_LIBWEBP=v1.4.0

# fontconfig
V_LIBXML2=v2.15.1

# libass
V_FRIBIDI=v1.0.16
V_FREETYPE=VER-2-13-3
V_HARFBUZZ=11.4.5
V_FONTCONFIG=2.17.1

# libplacebo
V_DOVI_TOOLS=2.3.1
V_LCMS=lcms2.17
V_SHADERC=v2025.4

# mpv
# FFmpeg: HelloZeroNick fork, feat-ohos (release/9.1 基线 + ohcodec HDR 元数据
# 与输出 buffer 计数)。原先指向 p1nbored/feat-ohos-hdr，那是 release/5.1
# 路线（libavcodec 62）；换到本 fork 后是 libavcodec 63。
V_FFMPEG=feat-ohos
V_LIBASS=0.17.4
V_LIBPLACEBO=v7.360.1
V_LUA=5.2.4
# mpv: HelloZeroNick fork, HDR patches + surface geometry fix
V_MPV=feat-ohos-surface-geometry
