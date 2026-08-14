#!/bin/bash

set -eu

PATCHES=(patches/*)
ROOT=$(pwd)

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
      else
        # 某些 patch 相对当前 fork 源码已失效(如 ffmpeg 的 av3a, 构建已禁用该特性), 跳过不中断
        echo "SKIP (does not apply): $patch"
      fi
    done
    popd
  fi
done
