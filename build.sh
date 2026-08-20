#!/bin/sh

set -e

make -j test
make -j test-win32

android_ndk_path=${ANDROID_NDK_ROOT:-}
android_sdk_path=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}

if [ -z "$android_sdk_path" ] && command -v adb >/dev/null 2>&1; then
    android_sdk_path=$(dirname "$(dirname "$(command -v adb)")")
fi

if [ -z "$android_ndk_path" ] && [ -n "$android_sdk_path" ]; then
    android_ndk_path="$android_sdk_path/ndk/29.0.14206865"
fi

if [ -x "$android_ndk_path/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android24-clang++" ]; then
    make -j test-android ANDROID_NDK_ROOT="$android_ndk_path" ANDROID_SDK_ROOT="$android_sdk_path"
else
    echo "Android configuration tests: SKIP (Android NDK 29 not found)"
fi
