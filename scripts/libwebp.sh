#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

. $ROOT_DIR/env.sh

pushd $ROOT_DIR/libmpv/libwebp

if [ "$1" == "build" ]; then
	echo -e "\nBuilding libwebp..."
elif [ "$1" == "clean" ]; then
	rm -rf .build
	exit 0
else
	exit 1
fi

mkdir -p .build
cd .build

# libwebp 用于 mpv 动态截图 webp 编码 (--ovc=libwebp_anim / libwebp_anim)
# 仅保留静态库 libwebp / libwebpmux, 其余工具全部关闭
cmake -L \
  -DCMAKE_TOOLCHAIN_FILE=$OHOS_TOOLCHAIN_FILE \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX=$DEST \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SKIP_RPATH=TRUE \
  -DCMAKE_C_FLAGS="$LTO_CFLAGS $PGO_CFLAGS" \
  -DWEBP_BUILD_ANIM_UTILS=OFF \
  -DWEBP_BUILD_CWEBP=OFF \
  -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF \
  -DWEBP_BUILD_IMG2WEBP=OFF \
  -DWEBP_BUILD_VWEBP=OFF \
  -DWEBP_BUILD_WEBPINFO=OFF \
  -DWEBP_BUILD_WEBPMUX=OFF \
  -DWEBP_BUILD_EXTRAS=OFF \
  -GNinja \
  ..
ninja -j$CORES
ninja install

popd
