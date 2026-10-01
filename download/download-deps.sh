#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

. $ROOT_DIR/download/deps-version.sh

pushd $ROOT_DIR/libmpv

# mbedtls
if [ ! -d mbedtls ]; then
  echo "Downloading mbedtls..."
	mkdir mbedtls
	wget -qO mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$V_MBEDTLS/mbedtls-$V_MBEDTLS.tar.bz2
  tar -C mbedtls --strip-components=1 -jxf mbedtls.tar.bz2
  rm mbedtls.tar.bz2
else
  echo "mbedtls already exists, skipping."
fi

# dav1d
if [ ! -d dav1d ]; then
  echo "Downloading dav1d..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_DAV1D https://code.videolan.org/videolan/dav1d.git dav1d > /dev/null
else
  echo "dav1d already exists, skipping."
fi

# libxml2
if [ ! -d libxml2 ]; then
  echo "Downloading libxml2..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_LIBXML2 --recursive https://gitlab.gnome.org/GNOME/libxml2.git libxml2 > /dev/null
else
  echo "libxml2 already exists, skipping."
fi

# fribidi
if [ ! -d fribidi ]; then
  echo "Downloading fribidi..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_FRIBIDI https://github.com/fribidi/fribidi.git fribidi > /dev/null
else
  echo "fribidi already exists, skipping."
fi

# freetype
if [ ! -d freetype ]; then
  echo "Downloading freetype..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_FREETYPE https://gitlab.freedesktop.org/freetype/freetype.git freetype > /dev/null
else
  echo "freetype already exists, skipping."
fi

# harfbuzz
if [ ! -d harfbuzz ]; then
  echo "Downloading harfbuzz..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_HARFBUZZ https://github.com/harfbuzz/harfbuzz.git harfbuzz > /dev/null
else
  echo "harfbuzz already exists, skipping."
fi

# fontconfig
if [ ! -d fontconfig ]; then
  echo "Downloading fontconfig..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_FONTCONFIG https://gitlab.freedesktop.org/fontconfig/fontconfig.git fontconfig > /dev/null
else
  echo "fontconfig already exists, skipping."
fi

# dovi_tools
if [ ! -d dovi_tools ]; then
  echo "Downloading dovi_tools..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_DOVI_TOOLS https://github.com/quietvoid/dovi_tool.git dovi_tools > /dev/null
else
  echo "dovi_tools already exists, skipping."
fi

# lcms
if [ ! -d lcms ]; then
  echo "Downloading lcms..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_LCMS https://github.com/mm2/Little-CMS.git lcms > /dev/null
else
  echo "lcms already exists, skipping."
fi

# shaderc
if [ ! -d shaderc ]; then
  echo "Downloading shaderc..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_SHADERC https://github.com/google/shaderc.git shaderc > /dev/null
  cd shaderc
  ./utils/git-sync-deps > /dev/null
  cd ..
else
  echo "shaderc already exists, skipping."
fi

# libwebp
if [ ! -d libwebp ]; then
  echo "Downloading libwebp..."
  mkdir libwebp
  wget -qO libwebp.tar.gz https://github.com/webmproject/libwebp/archive/refs/tags/$V_LIBWEBP.tar.gz
  tar -C libwebp --strip-components=1 -zxf libwebp.tar.gz
  rm libwebp.tar.gz
else
  echo "libwebp already exists, skipping."
fi

# ffmpeg
# FFMPEG_REPO/FFMPEG_REF mirror MPV_REPO/MPV_REF below: the HarmonyOS dynamic
# HDR metadata patches (prefix SEI extraction in ohdec.c) live in a fork, and
# a local clone works too:
#   FFMPEG_REPO=/path/to/ffmpeg FFMPEG_REF=feat-ohos-hdr ./download.sh
if [ ! -d ffmpeg ]; then
  echo "Downloading ffmpeg from ${FFMPEG_REPO:-https://github.com/p1nbored/FFmpeg.git} (${FFMPEG_REF:-$V_FFMPEG})..."
  git -c advice.detachedHead=false clone -q --depth 1 -b "${FFMPEG_REF:-$V_FFMPEG}" "${FFMPEG_REPO:-https://github.com/p1nbored/FFmpeg.git}" ffmpeg > /dev/null
else
  echo "ffmpeg already exists, skipping."
fi

# libass
if [ ! -d libass ]; then
  echo "Downloading libass..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_LIBASS https://github.com/libass/libass.git libass > /dev/null
else
  echo "libass already exists, skipping."
fi

# libplacebo
if [ ! -d libplacebo ]; then
  echo "Downloading libplacebo..."
  git -c advice.detachedHead=false clone -q --depth 1 -b $V_LIBPLACEBO --recursive https://code.videolan.org/videolan/libplacebo.git libplacebo > /dev/null
else
  echo "libplacebo already exists, skipping."
fi

# lua
if [ ! -d lua ]; then
  echo "Downloading lua..."
  mkdir lua
  wget -qO lua.tar.gz https://www.lua.org/ftp/lua-$V_LUA.tar.gz
  tar -C lua --strip-components=1 -zxf lua.tar.gz
  rm lua.tar.gz
else
  echo "lua already exists, skipping."
fi

# mpv
# MPV_REPO/MPV_REF allow building against a fork, which is how the HarmonyOS
# HDR patches get in without republishing the upstream branch. A local clone
# works too: MPV_REPO=/path/to/mpv MPV_REF=feat-ohos-hdr ./download.sh
if [ ! -d mpv ]; then
  echo "Downloading mpv from ${MPV_REPO:-https://github.com/p1nbored/mpv.git} (${MPV_REF:-$V_MPV})..."
  git -c advice.detachedHead=false clone -q --depth 1 -b "${MPV_REF:-$V_MPV}" "${MPV_REPO:-https://github.com/p1nbored/mpv.git}" mpv > /dev/null
else
  echo "mpv already exists, skipping."
fi

popd
