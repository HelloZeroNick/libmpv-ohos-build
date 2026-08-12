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
# ===== LTO / PGO 优化开关 (默认关闭) =====
# LTO=1            : 开启链接时优化 -flto (毕昇增强版)
# PGO=instrument   : PGO 插桩构建, 真机采集 profile 用
# PGO=use:/path    : 使用已采集的 profile 优化构建 (如 PGO=use:/root/lib.profdata)
# 用法: LTO=1 ./bundle.sh   或   PGO=instrument ./bundle.sh
# 注意: PGO 需先 instrument 采集, 再 use 构建; 二者互斥
if [ "${LTO:-0}" = "1" ]; then
  export ENABLE_LTO=1
  LTO_CFLAGS="-flto"
  echo "LTO: enabled (-flto)"
else
  export ENABLE_LTO=0
  LTO_CFLAGS=""
fi

export PGO_MODE="${PGO:-}"
PGO_CFLAGS=""
PGO_LDFLAGS=""
if [ -n "$PGO_MODE" ]; then
  case "$PGO_MODE" in
    instrument)
      # 毕昇 PGO 插桩: 运行时通过信号量把计数器写入真机沙箱目录
      # 真机映射: /data/app/el2/100/base/<bundleName>/files
      PGO_CFLAGS="-fprofile-generate=/data/storage/el2/base/files"
      PGO_LDFLAGS="-fprofile-generate=/data/storage/el2/base/files"
      echo "PGO: instrument (插桩, 需真机采集 profile)"
      ;;
    use:*)
      PGO_PROFILE="${PGO_MODE#use:}"
      # 注意: 不用 -fprofile-correction (毕昇 clang 15 不支持该选项, 会报
      # "optimization flag not supported"; meson 用 -Werror 会把警告升级为错误导致构建失败)
      PGO_CFLAGS="-fprofile-use=$PGO_PROFILE"
      PGO_LDFLAGS="-fprofile-use=$PGO_PROFILE"
      echo "PGO: use $PGO_PROFILE"
      ;;
    *)
      echo "Unknown PGO mode: $PGO_MODE (instrument | use:/path/lib.profdata)" >&2
      exit 1
      ;;
  esac
fi

export LTO_CFLAGS PGO_CFLAGS PGO_LDFLAGS

export CFLAGS="-fPIC -D__MUSL__=1 $LTO_CFLAGS $PGO_CFLAGS"
export CXXFLAGS="-fPIC -D__MUSL__=1 $LTO_CFLAGS $PGO_CFLAGS"

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

# ===== 生成 crossfile (注入 LTO/PGO 参数) =====
# 先删除 download.sh 建立的符号链接, 再生成含优化参数的实文件
# (否则 cp/sed 会跟随链接改写 crossfiles/ 下的源模板)
# (meson 项目: mpv/libplacebo/dav1d/libxml2/fribidi/freetype/harfbuzz/fontconfig/libass/lcms)
if [ "$(uname -s)" = "Linux" ]; then
  CROSSFILE_TEMPLATE=$ROOT_DIR/crossfiles/arm64-crossfile-linux.ini
else
  CROSSFILE_TEMPLATE=$ROOT_DIR/crossfiles/arm64-crossfile-macos.ini
fi
export CROSSFILE=$ROOT_DIR/libmpv/arm64-crossfile.ini
mkdir -p $ROOT_DIR/libmpv

rm -f "$CROSSFILE"
cp "$CROSSFILE_TEMPLATE" "$CROSSFILE"

# 将 PGO flags 转为 meson 数组元素 (每个参数单引号包裹, 避免 meson 解析错误)
# 例如: -fprofile-generate=/data/storage/el2/base/files
#   →    '-fprofile-generate=/data/storage/el2/base/files',
PGO_CFLAGS_MESON=""
for f in $PGO_CFLAGS; do
  PGO_CFLAGS_MESON="$PGO_CFLAGS_MESON '$f',"
done
PGO_LDFLAGS_MESON=""
for f in $PGO_LDFLAGS; do
  PGO_LDFLAGS_MESON="$PGO_LDFLAGS_MESON '$f',"
done

# 用临时文件 + mv 避免 GNU/BSD sed -i 差异
if [ "$ENABLE_LTO" = "1" ]; then
  sed '/^\[built-in options\]/a b_lto = true' "$CROSSFILE" > "$CROSSFILE.tmp" && mv "$CROSSFILE.tmp" "$CROSSFILE"
  echo "LTO: b_lto = true 注入 $CROSSFILE"
fi
if [ -n "$PGO_CFLAGS_MESON" ]; then
  sed -e "s|^c_args = \[|c_args = [$PGO_CFLAGS_MESON |" \
      -e "s|^cpp_args = \[|cpp_args = [$PGO_CFLAGS_MESON |" "$CROSSFILE" > "$CROSSFILE.tmp" && mv "$CROSSFILE.tmp" "$CROSSFILE"
fi
if [ -n "$PGO_LDFLAGS_MESON" ]; then
  sed -e "s|^c_link_args = \[|c_link_args = [$PGO_LDFLAGS_MESON |" \
      -e "s|^cpp_link_args = \[|cpp_link_args = [$PGO_LDFLAGS_MESON |" "$CROSSFILE" > "$CROSSFILE.tmp" && mv "$CROSSFILE.tmp" "$CROSSFILE"
fi
echo "Crossfile: $CROSSFILE (LTO=$ENABLE_LTO, PGO=$PGO_MODE)"
