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
android_test_memory_mb=4096
# API 24's goldfish DMA driver panics with the 4 GiB guest memory layout.
if [ "$android_test_expected_api" = 24 ]; then
    android_test_memory_mb=2048
fi
android_test_boot_timeout=${ANDROID_TEST_BOOT_TIMEOUT_SECONDS:-300}
android_test_runtime_timeout=${ANDROID_TEST_RUNTIME_TIMEOUT_SECONDS:-600}
for android_test_timeout in "$android_test_boot_timeout" "$android_test_runtime_timeout"; do
    if ! printf '%s\n' "$android_test_timeout" | grep -Eq '^[1-9][0-9]*$'; then
        echo "Android test timeouts must be positive integer seconds" >&2
        exit 2
    fi
done

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
    "$android_test_adb"; do
    if [ ! -x "$android_test_tool" ]; then
        echo "Required Android tool is not executable: $android_test_tool" >&2
        exit 2
    fi
done

if [ ! -x "$android_test_emulator" ]; then
    "$android_test_sdk_manager" --install emulator
fi
if [ ! -x "$android_test_emulator" ]; then
    echo "Required Android tool is not executable: $android_test_emulator" >&2
    exit 2
fi

if [ ! -x "$android_test_ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android24-clang++" ]; then
    echo "Android NDK $android_test_ndk_version is required" >&2
    exit 2
fi

if [ ! -f "$ANDROID_SDK_ROOT/system-images/android-$android_test_api/$android_test_tag/$android_test_abi/package.xml" ]; then
    "$android_test_sdk_manager" --install "$android_test_image"
fi

if [ -n "${ANDROID_EMULATOR_REVISION:-}" ]; then
    android_test_emulator_version=$("$android_test_emulator" -no-window -version)
    if ! printf '%s\n' "$android_test_emulator_version" | grep -Fq "Android emulator version $ANDROID_EMULATOR_REVISION."; then
        printf 'Android emulator revision mismatch: expected %s\n%s\n' \
            "$ANDROID_EMULATOR_REVISION" "$android_test_emulator_version" >&2
        exit 2
    fi
fi
if [ -n "${ANDROID_IMAGE_REVISION:-}" ] && ! grep -Fxq "Pkg.Revision=$ANDROID_IMAGE_REVISION" \
    "$ANDROID_SDK_ROOT/system-images/android-$android_test_api/$android_test_tag/$android_test_abi/source.properties"; then
    echo "Android system image revision mismatch: expected $ANDROID_IMAGE_REVISION ($android_test_image)" >&2
    exit 2
fi

android_test_temp_base=${TMPDIR:-/tmp}
android_test_temp_prefix=$android_test_temp_base/cardio-android.
android_test_root=$(mktemp -d "$android_test_temp_prefix"XXXXXX)
android_test_diagnostics_base=${ANDROID_TEST_DIAGNOSTICS_DIR:-build/android/diagnostics}
mkdir -p "$android_test_diagnostics_base"
android_test_diagnostics_dir=$(mktemp -d "$android_test_diagnostics_base/run.XXXXXX")
android_test_log=$android_test_diagnostics_dir/emulator.log
android_test_pid=
android_test_logcat_pid=

collect_android_diagnostic() {
    android_diagnostic_file=$1
    android_diagnostic_timeout=$2
    shift 2
    android_diagnostic_status=0
    timeout --kill-after=5s "$android_diagnostic_timeout" \
        "$android_test_adb" -s "$android_test_serial" "$@" \
        > "$android_test_diagnostics_dir/$android_diagnostic_file" 2>&1 || android_diagnostic_status=$?
    printf '%s: exit %s\n' "$android_diagnostic_file" "$android_diagnostic_status" \
        >> "$android_test_diagnostics_dir/collection-status.txt"
}

cleanup_android_test() {
    android_test_status=$?
    trap - EXIT INT TERM
    # Diagnostics must neither replace the test result nor prevent emulator cleanup.
    set +e

    printf 'test exit status: %s\n' "$android_test_status" \
        > "$android_test_diagnostics_dir/collection-status.txt"
    if [ "$android_test_status" -ne 0 ] && [ -n "$android_test_pid" ]; then
        # Capture guest state before shutdown, even when system_server has died.
        collect_android_diagnostic logcat-snapshot.log 20s logcat -b all -d -v threadtime
        collect_android_diagnostic getprop.txt 20s shell getprop
        collect_android_diagnostic dropbox.txt 30s shell dumpsys dropbox --print
        collect_android_diagnostic bugreport.log 120s bugreport "$android_test_diagnostics_dir/bugreport.zip"
    fi

    if [ -n "$android_test_logcat_pid" ]; then
        # The read-only streaming client can hang on SIGTERM. Diagnostics are
        # already saved, so force its termination before waiting for cleanup.
        kill -KILL "$android_test_logcat_pid" >/dev/null 2>&1 || true
        wait "$android_test_logcat_pid" >/dev/null 2>&1 || true
    fi

    if [ -n "$android_test_pid" ]; then
        timeout --kill-after=5s 10s "$android_test_adb" -s "$android_test_serial" emu kill >/dev/null 2>&1 || true
        if kill -0 "$android_test_pid" >/dev/null 2>&1; then
            kill "$android_test_pid" >/dev/null 2>&1 || true
        fi
        wait "$android_test_pid" >/dev/null 2>&1 || true
    fi

    if [ "$android_test_status" -ne 0 ] && [ -f "$android_test_log" ]; then
        tail -n 200 "$android_test_log" >&2
    fi
    printf 'Android diagnostics: %s\n' "$android_test_diagnostics_dir"

    case "$android_test_root" in
        "$android_test_temp_prefix"*) rm -rf -- "$android_test_root" ;;
    esac
    exit "$android_test_status"
}
trap cleanup_android_test EXIT INT TERM

{
    printf 'API: %s\nSystem image: %s\nExpected page size: %s\n' \
        "$android_test_expected_api" "$android_test_image" "$android_test_page_size"
    printf 'Requested RAM (MiB): %s\n' "$android_test_memory_mb"
    printf 'Boot timeout (s): %s\nRuntime timeout (s): %s\n' \
        "$android_test_boot_timeout" "$android_test_runtime_timeout"
    "$android_test_adb" version
    for android_test_properties in \
        "$(dirname "$android_test_emulator")/source.properties" \
        "$ANDROID_SDK_ROOT/system-images/android-$android_test_api/$android_test_tag/$android_test_abi/source.properties"; do
        if [ -f "$android_test_properties" ]; then
            cat "$android_test_properties"
            printf '\n'
        fi
    done
} > "$android_test_diagnostics_dir/environment.txt" 2>&1

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
    -show-kernel \
    -no-snapshot \
    -wipe-data \
    -memory "$android_test_memory_mb" \
    -gpu swiftshader_indirect \
    >"$android_test_log" 2>&1 &
android_test_pid=$!

android_test_boot_deadline=$(( $(date +%s) + android_test_boot_timeout ))
if ! timeout --kill-after=5s "${android_test_boot_timeout}s" \
    "$android_test_adb" -s "$android_test_serial" wait-for-device; then
    echo "Android emulator did not connect to adb" >&2
    exit 1
fi
# Keep a host-side copy so a guest restart cannot erase the crash evidence.
"$android_test_adb" -s "$android_test_serial" logcat -b all -v threadtime \
    > "$android_test_diagnostics_dir/logcat.log" 2>&1 &
android_test_logcat_pid=$!
while :; do
    if ! kill -0 "$android_test_pid" >/dev/null 2>&1; then
        echo "Android emulator exited before boot completed" >&2
        exit 1
    fi
    android_test_boot_remaining=$(( android_test_boot_deadline - $(date +%s) ))
    if [ "$android_test_boot_remaining" -le 0 ]; then
        echo "Timed out waiting for Android emulator boot" >&2
        exit 1
    fi
    android_test_boot_state=$(timeout --kill-after=5s "${android_test_boot_remaining}s" \
        "$android_test_adb" -s "$android_test_serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
    if [ "$android_test_boot_state" = 1 ]; then
        break
    fi
    sleep 1
done

timeout --kill-after=5s 20s "$android_test_adb" -s "$android_test_serial" shell cat /proc/meminfo \
    > "$android_test_diagnostics_dir/memory.txt"
android_test_server_pid=$(timeout --kill-after=5s 20s "$android_test_adb" -s "$android_test_serial" shell pidof system_server | tr -d '\r')
if [ -z "$android_test_server_pid" ]; then
    echo "system_server is not running" >&2
    exit 1
fi
printf 'system_server before tests: %s\n' "$android_test_server_pid" \
    >> "$android_test_diagnostics_dir/environment.txt"

timeout --kill-after=5s "${android_test_runtime_timeout}s" \
    make -j "${ANDROID_TEST_TARGET:-test-android-runtime}" \
    ANDROID_NDK_ROOT="$android_test_ndk_root" \
    ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    ANDROID_RUNTIME_ABI="$android_test_abi" \
    ANDROID_EXPECTED_API="$android_test_expected_api" \
    ANDROID_EXPECTED_PAGE_SIZE="$android_test_page_size" \
    ADB="$android_test_adb" \
    ADB_SERIAL="$android_test_serial"

android_test_server_after=$(timeout --kill-after=5s 20s "$android_test_adb" -s "$android_test_serial" shell pidof system_server | tr -d '\r')
printf 'system_server after tests: %s\n' "$android_test_server_after" \
    >> "$android_test_diagnostics_dir/environment.txt"
if [ "$android_test_server_after" != "$android_test_server_pid" ]; then
    echo "system_server restarted during the tests" >&2
    exit 1
fi

if grep -Fq 'Kernel panic - not syncing' "$android_test_log"; then
    echo "Android kernel panicked during this emulator run" >&2
    exit 1
fi

# Include crashes during boot, even if Android recovered before the tests ran.
timeout --kill-after=5s 20s "$android_test_adb" -s "$android_test_serial" \
    logcat -b crash -d -v threadtime > "$android_test_diagnostics_dir/crash-buffer.log" 2>&1
if grep -Eq 'FATAL EXCEPTION IN SYSTEM PROCESS|Fatal signal .*\(system_server\)|>>> system_server <<<' \
    "$android_test_diagnostics_dir/crash-buffer.log" "$android_test_diagnostics_dir/logcat.log"; then
    echo "system_server crashed during this emulator run" >&2
    exit 1
fi
