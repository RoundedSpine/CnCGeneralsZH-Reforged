#!/usr/bin/env bash
# An unsigned arm64 device IPA, using the native SDL GPU Metal renderer.
set -euo pipefail
code=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$code/../.." && pwd)
build="${1:-$repo/build/ios}"
mkdir -p "$build"
build=$(cd "$build" && pwd)
[ "$(uname -s)" = Darwin ] || { echo 'iOS builds require macOS and Xcode.' >&2; exit 1; }
"$code/Tools/vendor.sh" --skip-art
sdk=$(xcrun --sdk iphoneos --show-sdk-path)
# Fresh Actions checkouts have new mtimes; an object cache alone still recompiles
# every source. ccache reuses compilation by content, with the device flags in its key.
launcher=$(command -v ccache || true)
cmake -S "$code" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_C_COMPILER="$(xcrun --sdk iphoneos --find clang)" \
  -DCMAKE_CXX_COMPILER="$(xcrun --sdk iphoneos --find clang++)" \
  -DCMAKE_C_COMPILER_LAUNCHER="$launcher" -DCMAKE_CXX_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_OBJC_COMPILER_LAUNCHER="$launcher" -DCMAKE_OBJCXX_COMPILER_LAUNCHER="$launcher" \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build "$build" --target generals zh_overlay --parallel "${ZH_BUILD_JOBS:-3}" -- -k 0
"$code/Tools/make-ios-ipa.sh" "$build"
