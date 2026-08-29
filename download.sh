#!/bin/bash

set -eu

mkdir -p ./libmpv/arm64-build

if [ "$(uname -s)" = "Linux" ]; then
  if [ ! -d /sdk/command-line-tools ]; then
    echo "Downloading HarmonyOS SDK..."
    ./download/download-sdk.sh
  fi
  # 必须在 SDK 判断之外: SDK 已存在时也要建这个链接, 否则 meson 会因为
  # 找不到 libmpv/arm64-crossfile.ini 而失败。
  ln -sf ../crossfiles/arm64-crossfile-linux.ini ./libmpv/arm64-crossfile.ini
elif [ "$(uname -s)" = "Darwin" ]; then
  echo "Using DevEco Studio for macOS, please make sure DevEco Studio is installed."
  ln -sf ../crossfiles/arm64-crossfile-macos.ini ./libmpv/arm64-crossfile.ini
else
  echo "Unsupported platform." >&2
  exit 1
fi

./download/download-ohos-rs.sh
./download/download-deps.sh
