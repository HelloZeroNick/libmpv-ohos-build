#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

if [ "$(uname -s)" = "Linux" ]; then
  export OHOS_NDK_HOME=/sdk/linux
  export CORES=$(nproc)
elif [ "$(uname -s)" = "Darwin" ]; then
  export OHOS_NDK_HOME=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
  export CORES=$(sysctl -n hw.ncpu)
fi

export DEST=$ROOT_DIR/libmpv/arm64-build
export PATH=$OHOS_NDK_HOME/native/build-tools/cmake/bin:$PATH
export PKG_CONFIG_PATH=$DEST/lib/pkgconfig
export PKG_CONFIG_LIBDIR=$OHOS_NDK_HOME/native/sysroot/usr/lib
export PKG_CONFIG_INCLUDEDIR=$OHOS_NDK_HOME/native/sysroot/usr/include

# 优先使用毕昇编译器 (BiSheng), 不存在则回退开源 LLVM
if [ "$(uname -s)" = "Linux" ]; then
  BISHENG_HOME=/sdk/command-line-tools/sdk/default/hms/native/BiSheng
else
  BISHENG_HOME=$(dirname $OHOS_NDK_HOME)/hms/native/BiSheng
fi

if [ -d "$BISHENG_HOME" ]; then
  export TOOLCHAIN_HOME=$BISHENG_HOME
  echo "Using BiSheng compiler: $BISHENG_HOME"
else
  export TOOLCHAIN_HOME=$OHOS_NDK_HOME/native/llvm
  echo "BiSheng not found, falling back to LLVM: $TOOLCHAIN_HOME"
fi

export AS=$TOOLCHAIN_HOME/bin/llvm-as
export CC="$TOOLCHAIN_HOME/bin/clang --target=aarch64-linux-ohos --sysroot=$OHOS_NDK_HOME/native/sysroot"
export CXX="$TOOLCHAIN_HOME/bin/clang++ --target=aarch64-linux-ohos --sysroot=$OHOS_NDK_HOME/native/sysroot"
export LD=$TOOLCHAIN_HOME/bin/ld.lld
export STRIP=$TOOLCHAIN_HOME/bin/llvm-strip
export RANLIB=$TOOLCHAIN_HOME/bin/llvm-ranlib
export OBJDUMP=$TOOLCHAIN_HOME/bin/llvm-objdump
export OBJCOPY=$TOOLCHAIN_HOME/bin/llvm-objcopy
export NM=$TOOLCHAIN_HOME/bin/llvm-nm
export AR=$TOOLCHAIN_HOME/bin/llvm-ar
export CFLAGS='-fPIC -D__MUSL__=1'
export CXXFLAGS='-fPIC -D__MUSL__=1'

# 生成指向当前工具链的 cmake toolchain 文件副本
# (原 ohos.toolchain.cmake 硬编码使用 openharmony/native/llvm,
#  这里将其替换为 TOOLCHAIN_HOME, 使 cmake 项目如 shaderc 也使用 BiSheng)
# 注意: 副本不在原目录, 必须同时把 CMAKE_CURRENT_LIST_DIR 替换为绝对路径,
#       否则 OHOS_SDK_NATIVE / oh-uni-package.json / sdk_native_platforms.cmake
#       / CMAKE_SYSROOT 等相对路径计算会全部失效。
export OHOS_TOOLCHAIN_FILE=$DEST/ohos-$(basename $TOOLCHAIN_HOME).toolchain.cmake
mkdir -p $DEST
sed \
  -e "s|\${CMAKE_CURRENT_LIST_DIR}|$OHOS_NDK_HOME/native/build/cmake|g" \
  -e "s|\${OHOS_SDK_NATIVE}/llvm|$TOOLCHAIN_HOME|g" \
  $OHOS_NDK_HOME/native/build/cmake/ohos.toolchain.cmake \
  > $OHOS_TOOLCHAIN_FILE
echo "CMake toolchain file: $OHOS_TOOLCHAIN_FILE"
