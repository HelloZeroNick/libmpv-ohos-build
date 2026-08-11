#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

. $ROOT_DIR/env.sh

pushd $ROOT_DIR/libmpv/mpv

if [ "$1" == "build" ]; then
	echo -e "\nBuilding mpv..."
elif [ "$1" == "clean" ]; then
	rm -rf .build
	exit 0
else
	exit 1
fi

mkdir -p .build
cd .build

# 体积优化: 显式禁用 OHOS 播放器用不到的功能
# (dvdnav/libarchive/rubberband/uchardet/javascript 等在交叉编译下通常会
#  因找不到库自动禁用, 这里显式关闭以防 CI 环境变化时被意外启用)
meson setup .. \
  --cross-file $ROOT_DIR/libmpv/arm64-crossfile.ini \
  --prefix=$DEST/mpv \
  --default-library shared \
  --strip \
  -Dopensles=disabled \
  -Dohos=enabled \
  -Degl-ohos=enabled \
  -Dvulkan=enabled \
  -Dshaderc=enabled \
  -Dlua=enabled \
  -Dgpl=false \
  -Dbuild-date=false \
  -Dcplayer=false \
  -Dmanpage-build=disabled \
  \
  -Djavascript=disabled \
  -Ddvdnav=disabled \
  -Dlibarchive=disabled \
  -Drubberband=disabled \
  -Duchardet=disabled \
  -Dlibbluray=disabled \
  -Dcdda=disabled \
  -Ddvbin=disabled \
  -Dvapoursynth=disabled \
  -Dzimg=disabled \
  -Dsdl2-audio=disabled \
  -Djack=disabled \
  -Dpipewire=disabled \
  -Dpulse=disabled \
  -Dalsa=disabled \
  -Dopenal=disabled \
  -Dsdl2-gamepad=disabled \
  -Dcplugins=disabled \
  -Dlibavdevice=disabled
ninja -j$CORES
ninja install

cd $DEST/mpv/lib
mv libmpv.so ../../libmpv.so

popd