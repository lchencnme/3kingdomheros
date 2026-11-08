#!/bin/sh
# Fetch the pinned GBDK-2020 toolchain (design doc 11.1) into tools/gbdk.
set -e
cd "$(dirname "$0")"
VER=4.5.0
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) PKG=gbdk-macos-arm64.tar.gz; SHA=289ee60e46c5a2785a21e35533f84a5131ed4a063b21b0dbdedc9a10af15bf78 ;;
  Darwin-x86_64) PKG=gbdk-macos.tar.gz; SHA= ;;
  Linux-x86_64) PKG=gbdk-linux64.tar.gz; SHA= ;;
  Linux-aarch64) PKG=gbdk-linux-arm64.tar.gz; SHA= ;;
  *) echo "unsupported host"; exit 1 ;;
esac
mkdir -p dl
curl -sSL -o dl/$PKG https://github.com/gbdk-2020/gbdk-2020/releases/download/$VER/$PKG
if [ -n "$SHA" ]; then echo "$SHA  dl/$PKG" | shasum -a 256 -c -; fi
tar xzf dl/$PKG -C .
rm -rf dl
./gbdk/bin/sdcc --version | head -1
