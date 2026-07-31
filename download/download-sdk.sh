#!/bin/bash

set -eu

. ./download/deps-version.sh

pushd /

sudo wget -qO sdk.tar.gz https://repo.huaweicloud.com/openharmony/os/$V_SDK/ohos-sdk-windows_linux-public.tar.gz
sudo mkdir -p sdk
sudo tar -C sdk -zxf sdk.tar.gz
sudo rm sdk.tar.gz

cd sdk/ohos-sdk
sudo rm -rf windows/
sudo rm -rf ohos/
sudo mv linux ./../

# Extract NDK
cd linux
for i in *.zip
do
  sudo unzip -q $i
  sudo rm $i
done

popd
