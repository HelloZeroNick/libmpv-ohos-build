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
mkdir -p $DEST/include $DEST/lib
cp -rp include/mbedtls $DEST/include/
cp -rp tf-psa-crypto/drivers/builtin/include/mbedtls $DEST/include/
mkdir -p $DEST/include/psa
cp -rp tf-psa-crypto/include/psa $DEST/include/
cp -RP library/libmbedtls.a $DEST/lib/
cp -RP library/libmbedx509.a $DEST/lib/
cp -RP library/libmbedcrypto.a $DEST/lib/

popd