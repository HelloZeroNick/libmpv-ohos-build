#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

. $ROOT_DIR/env.sh

pushd $ROOT_DIR/libmpv/mbedtls

if [ "$1" == "build" ]; then
	echo -e "\nBuilding mbedtls..."
elif [ "$1" == "clean" ]; then
	make clean
	exit 0
else
	exit 1
fi

python3 -m venv .venv
source .venv/bin/activate
pip install -r scripts/basic.requirements.txt

# 只构建静态库, 跳过 programs/fuzz/tests
# (make no_test 会构建 programs 含 fuzz, PGO 插桩下 fuzz 链接缺 profile runtime 会失败;
#  且这些程序我们根本不需要)
make -j$CORES lib

# 手动安装库 + 头文件 (等价于顶层 make install 的核心逻辑, 但避免触发 no_test 构建 fuzz)
# mbedtls 3.6.x 是仓库拆分前版本, 头文件在 include/mbedtls + include/psa
# (tf-psa-crypto/ 目录是 4.0 拆分后的, 3.6.4 不存在, 故容错处理)
mkdir -p $DEST/include/mbedtls $DEST/include/psa
cp -rp include/mbedtls/* $DEST/include/mbedtls/ 2>/dev/null || true
# 兼容旧版含 psa 头文件的布局
cp -rp include/psa/* $DEST/include/psa/ 2>/dev/null || true
# 兼容未来若升级到含 tf-psa-crypto 目录的版本
cp -rp tf-psa-crypto/include/psa/* $DEST/include/psa/ 2>/dev/null || true
cp -rp tf-psa-crypto/drivers/builtin/include/mbedtls/* $DEST/include/mbedtls/ 2>/dev/null || true
cp -RP library/libmbedtls.a $DEST/lib/ 2>/dev/null || true
cp -RP library/libmbedx509.a $DEST/lib/ 2>/dev/null || true
cp -RP library/libmbedcrypto.a $DEST/lib/ 2>/dev/null || true

# 校验关键文件齐全 (缺失即构建失败, 避免下游 ffmpeg 用残缺头文件)
for f in \
  $DEST/include/mbedtls/build_info.h \
  $DEST/include/psa/crypto.h \
  $DEST/lib/libmbedtls.a \
  $DEST/lib/libmbedx509.a \
  $DEST/lib/libmbedcrypto.a; do
  if [ ! -f "$f" ]; then
    echo "ERROR: mbedtls 安装不完整, 缺少 $f" >&2
    exit 1
  fi
done
echo "mbedtls 安装完成: libmbedtls/libmbedx509/libmbedcrypto + 头文件"

popd