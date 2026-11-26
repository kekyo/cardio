#!/bin/sh

set -e

# Each concurrent C++ compilation also starts a CodeQL extractor in CI.
build_jobs=4
case "$(uname -sm)" in
    Darwin\ *) exec gmake -j"$build_jobs" test-macos ;;
    Linux\ x86_64) make -j"$build_jobs" macos-cross ;;
esac
make -j"$build_jobs" test
make -j"$build_jobs" test-win32

android_ndk_path=${ANDROID_NDK_ROOT:-}
android_sdk_path=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}

if [ -z "$android_sdk_path" ] && command -v adb >/dev/null 2>&1; then
    android_sdk_path=$(dirname "$(dirname "$(command -v adb)")")
fi

if [ -z "$android_ndk_path" ] && [ -n "$android_sdk_path" ]; then
    android_ndk_path="$android_sdk_path/ndk/29.0.14206865"
fi

if [ -x "$android_ndk_path/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android24-clang++" ]; then
    make -j"$build_jobs" test-android ANDROID_NDK_ROOT="$android_ndk_path" ANDROID_SDK_ROOT="$android_sdk_path"
else
    echo "Android configuration tests: SKIP (Android NDK 29 not found)"
fi
