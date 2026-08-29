#!/bin/bash

set -eu

PATCHES=(patches/*)
ROOT=$(pwd)

# 允许静默跳过的 patch 白名单; 其余任何 patch 应用失败都会中断构建。
OPTIONAL_PATCHES=(
  patches/ffmpeg/support-av3a-audio-vivid.patch
)

for dep_path in "${PATCHES[@]}"; do
  if [ -d "$dep_path" ]; then
    patches=($dep_path/*)
    dep=${dep_path#*/}
    pushd ./libmpv/$dep
    echo "Patching $dep..."
    for patch in "${patches[@]}"; do
      echo "Applying $patch..."
      if git apply "$ROOT/$patch" 2> /dev/null; then
        echo "OK $patch"
      elif [[ " ${OPTIONAL_PATCHES[*]} " == *" $patch "* ]]; then
        # 已知相对当前 fork 源码已失效, 且构建已禁用该特性, 跳过不中断
        echo "SKIP (known-stale, optional): $patch"
      else
        # 其余 patch 是必需的(如 libplacebo 的零拷贝路径), 静默跳过会造成难以
        # 察觉的功能退化, 因此直接失败
        echo "FAIL (does not apply): $patch" >&2
        exit 1
      fi
    done
    popd
  fi
done
