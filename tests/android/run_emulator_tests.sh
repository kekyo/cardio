#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 API SYSTEM_IMAGE_TAG PAGE_SIZE" >&2
    exit 2
fi

android_test_api=$1
android_test_tag=$2
android_test_page_size=$3
android_test_expected_api=${ANDROID_EXPECTED_API:-${android_test_api%%.*}}
android_test_abi=x86_64
android_test_serial=emulator-5554
android_test_ndk_version=29.0.14206865

if [ -z "${ANDROID_SDK_ROOT:-}" ]; then
    echo "ANDROID_SDK_ROOT is required" >&2
    exit 2
fi

android_test_ndk_root=${ANDROID_NDK_ROOT:-$ANDROID_SDK_ROOT/ndk/$android_test_ndk_version}
android_test_sdk_manager=${ANDROID_SDK_MANAGER:-$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager}
android_test_avd_manager=${ANDROID_AVD_MANAGER:-$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/avdmanager}
android_test_emulator=${ANDROID_EMULATOR:-$ANDROID_SDK_ROOT/emulator/emulator}
android_test_adb=${ADB:-$ANDROID_SDK_ROOT/platform-tools/adb}
android_test_image="system-images;android-$android_test_api;$android_test_tag;$android_test_abi"

for android_test_tool in \
    "$android_test_sdk_manager" \
    "$android_test_avd_manager" \
    "$android_test_emulator" \
    "$android_test_adb"; do
    if [ ! -x "$android_test_tool" ]; then
        echo "Required Android tool is not executable: $android_test_tool" >&2
        exit 2
    fi
done

if [ ! -x "$android_test_ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android24-clang++" ]; then
    echo "Android NDK $android_test_ndk_version is required" >&2
    exit 2
fi

if [ ! -f "$ANDROID_SDK_ROOT/system-images/android-$android_test_api/$android_test_tag/$android_test_abi/package.xml" ]; then
    "$android_test_sdk_manager" --install "$android_test_image"
fi

android_test_temp_base=${TMPDIR:-/tmp}
android_test_temp_prefix=$android_test_temp_base/cardio-android.
android_test_root=$(mktemp -d "$android_test_temp_prefix"XXXXXX)
android_test_log=$android_test_root/emulator.log
android_test_pid=

cleanup_android_test() {
    android_test_status=$?
    trap - EXIT INT TERM

    if [ -n "$android_test_pid" ]; then
        "$android_test_adb" -s "$android_test_serial" emu kill >/dev/null 2>&1 || true
        if kill -0 "$android_test_pid" >/dev/null 2>&1; then
            kill "$android_test_pid" >/dev/null 2>&1 || true
        fi
        wait "$android_test_pid" >/dev/null 2>&1 || true
    fi

    if [ "$android_test_status" -ne 0 ] && [ -f "$android_test_log" ]; then
        tail -n 200 "$android_test_log" >&2
    fi

    case "$android_test_root" in
        "$android_test_temp_prefix"*) rm -rf -- "$android_test_root" ;;
    esac
    exit "$android_test_status"
}
trap cleanup_android_test EXIT INT TERM

export ANDROID_USER_HOME=$android_test_root/user
export ANDROID_AVD_HOME=$android_test_root/avd
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_AVD_HOME"

printf 'no\n' | "$android_test_avd_manager" create avd \
    --force \
    --name cardio-test \
    --package "$android_test_image"

"$android_test_emulator" \
    -avd cardio-test \
    -port 5554 \
    -no-window \
    -no-audio \
    -no-boot-anim \
    -no-snapshot \
    -wipe-data \
    -gpu swiftshader_indirect \
    >"$android_test_log" 2>&1 &
android_test_pid=$!

"$android_test_adb" -s "$android_test_serial" wait-for-device
android_test_boot_deadline=$(( $(date +%s) + 300 ))
while [ "$("$android_test_adb" -s "$android_test_serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]; do
    if ! kill -0 "$android_test_pid" >/dev/null 2>&1; then
        echo "Android emulator exited before boot completed" >&2
        exit 1
    fi
    if [ "$(date +%s)" -ge "$android_test_boot_deadline" ]; then
        echo "Timed out waiting for Android emulator boot" >&2
        exit 1
    fi
    sleep 1
done

make -j test-android-runtime \
    ANDROID_NDK_ROOT="$android_test_ndk_root" \
    ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    ANDROID_RUNTIME_ABI="$android_test_abi" \
    ANDROID_EXPECTED_API="$android_test_expected_api" \
    ANDROID_EXPECTED_PAGE_SIZE="$android_test_page_size" \
    ADB="$android_test_adb" \
    ADB_SERIAL="$android_test_serial"
