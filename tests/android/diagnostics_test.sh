#!/bin/bash

set -eu

diagnostics_test_runner=$(cd "$(dirname "$0")" && pwd)/run_emulator_tests.sh
diagnostics_test_root=$(mktemp -d)
trap 'rm -rf -- "$diagnostics_test_root"' EXIT

mkdir -p "$diagnostics_test_root/bin" \
    "$diagnostics_test_root/sdk/system-images/android-35/google_apis_ps16k/x86_64" \
    "$diagnostics_test_root/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
touch "$diagnostics_test_root/sdk/system-images/android-35/google_apis_ps16k/x86_64/package.xml"
printf '#!/bin/sh\nexit 0\n' > "$diagnostics_test_root/bin/sdkmanager"
cp "$diagnostics_test_root/bin/sdkmanager" "$diagnostics_test_root/bin/avdmanager"
cp "$diagnostics_test_root/bin/sdkmanager" \
    "$diagnostics_test_root/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android24-clang++"

cat > "$diagnostics_test_root/bin/emulator" <<'EOF'
#!/bin/sh
if [ "${1:-}" = -version ]; then
    echo 'GUI dependencies are unavailable on the headless CI runner' >&2
    exit 127
fi
if [ "$*" = '-no-window -version' ]; then
    echo "Android emulator version $DIAGNOSTICS_TEST_EMULATOR_VERSION.0 (test double)"
    exit 0
fi
touch "$DIAGNOSTICS_TEST_CASE/started"
echo 'emulator diagnostic output'
if [ "$DIAGNOSTICS_TEST_CRASH" = kernel ]; then
    echo 'Kernel panic - not syncing: Fatal exception'
fi
printf 'ready\n' > "$DIAGNOSTICS_TEST_CASE/ready"
read -r stop < "$DIAGNOSTICS_TEST_CASE/stop"
EOF

cat > "$diagnostics_test_root/bin/adb" <<'EOF'
#!/bin/sh
set -eu
if [ "$1" = '-s' ]; then shift 2; fi
case "$*" in
    wait-for-device)
        read -r ready < "$DIAGNOSTICS_TEST_CASE/ready"
        if [ "$DIAGNOSTICS_TEST_BOOT" = device ]; then
            read -r blocked < "$DIAGNOSTICS_TEST_CASE/boot-blocked"
        fi
        ;;
    'shell getprop sys.boot_completed')
        if [ "$DIAGNOSTICS_TEST_BOOT" = query ]; then
            read -r blocked < "$DIAGNOSTICS_TEST_CASE/boot-blocked"
        fi
        echo 1
        ;;
    'shell cat /proc/meminfo') echo 'MemTotal: 4096000 kB' ;;
    'shell pidof system_server')
        if [ "$DIAGNOSTICS_TEST_CRASH" = restart ] && [ -f "$DIAGNOSTICS_TEST_CASE/test-completed" ]; then
            echo 901
        else
            echo 567
        fi
        ;;
    'emu kill')
        touch "$DIAGNOSTICS_TEST_CASE/stopped"
        printf 'stop\n' > "$DIAGNOSTICS_TEST_CASE/stop"
        ;;
    version) echo 'Android Debug Bridge test double' ;;
    *)
        if [ -f "$DIAGNOSTICS_TEST_CASE/stopped" ]; then
            echo 'diagnostics requested after emulator shutdown' >&2
            exit 9
        fi
        case "$*" in
            logcat*)
                echo 'logcat diagnostics: test crash details'
                case "$DIAGNOSTICS_TEST_CRASH" in
                    native) echo 'F libc: Fatal signal 11 (SIGSEGV), pid 567 (system_server)' ;;
                    java) echo 'E AndroidRuntime: FATAL EXCEPTION IN SYSTEM PROCESS: main' ;;
                    other) echo 'F libc: Fatal signal 11 (SIGSEGV), pid 890 (unrelated_app)' ;;
                    stream)
                        if [ "$*" = 'logcat -b all -v threadtime' ]; then
                            echo 'F libc: Fatal signal 11 (SIGSEGV), pid 567 (system_server)'
                        fi
                        ;;
                esac
                if [ "$*" = 'logcat -b all -v threadtime' ]; then
                    if [ "$DIAGNOSTICS_TEST_LOGCAT_HANG" = 1 ]; then
                        trap '' TERM
                        echo $$ > "$DIAGNOSTICS_TEST_CASE/logcat.pid"
                    fi
                    printf 'ready\n' > "$DIAGNOSTICS_TEST_CASE/logcat-ready"
                    if [ "$DIAGNOSTICS_TEST_LOGCAT_HANG" = 1 ]; then
                        read -r blocked < "$DIAGNOSTICS_TEST_CASE/boot-blocked"
                    fi
                fi
                ;;
            'shell getprop') echo '[ro.build.version.sdk]: [35]' ;;
            'shell dumpsys dropbox --print') echo 'system_server_crash: test stack trace' ;;
            bugreport*) printf 'test bugreport\n' > "$2" ;;
            *) echo "Unexpected adb command: $*" >&2; exit 8 ;;
        esac
        exit "$DIAGNOSTICS_TEST_COLLECTION_STATUS"
        ;;
esac
EOF

cat > "$diagnostics_test_root/bin/make" <<'EOF'
#!/bin/sh
read -r ready < "$DIAGNOSTICS_TEST_CASE/logcat-ready"
echo 'Android runtime test output'
if [ "$DIAGNOSTICS_TEST_BOOT" = runtime ]; then
    read -r blocked < "$DIAGNOSTICS_TEST_CASE/boot-blocked"
fi
touch "$DIAGNOSTICS_TEST_CASE/test-completed"
exit "$DIAGNOSTICS_TEST_RESULT"
EOF

chmod +x "$diagnostics_test_root/bin/"* \
    "$diagnostics_test_root/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/"*

export ANDROID_SDK_ROOT="$diagnostics_test_root/sdk"
export ANDROID_NDK_ROOT="$diagnostics_test_root/ndk"
export ANDROID_SDK_MANAGER="$diagnostics_test_root/bin/sdkmanager"
export ANDROID_AVD_MANAGER="$diagnostics_test_root/bin/avdmanager"
export ANDROID_EMULATOR="$diagnostics_test_root/bin/emulator"
export ANDROID_EMULATOR_REVISION=37.2.12
export ANDROID_IMAGE_REVISION=5
export ADB="$diagnostics_test_root/bin/adb"
export PATH="$diagnostics_test_root/bin:$PATH"

for diagnostics_test_case in failure collection_failure success native java stream restart other kernel stale_emulator stale_image runtime_timeout boot_device boot_query stuck_logcat stuck_logcat_failure; do
    export DIAGNOSTICS_TEST_CASE="$diagnostics_test_root/$diagnostics_test_case"
    export ANDROID_TEST_DIAGNOSTICS_DIR="$DIAGNOSTICS_TEST_CASE/saved logs"
    export DIAGNOSTICS_TEST_RESULT=23
    export DIAGNOSTICS_TEST_COLLECTION_STATUS=0
    export DIAGNOSTICS_TEST_CRASH=none
    export DIAGNOSTICS_TEST_EMULATOR_VERSION=37.2.12
    export DIAGNOSTICS_TEST_BOOT=ready
    export DIAGNOSTICS_TEST_LOGCAT_HANG=0
    export ANDROID_TEST_BOOT_TIMEOUT_SECONDS=300
    export ANDROID_TEST_RUNTIME_TIMEOUT_SECONDS=600
    printf 'Pkg.Revision=5\n' > "$ANDROID_SDK_ROOT/system-images/android-35/google_apis_ps16k/x86_64/source.properties"
    diagnostics_test_expected_status=23
    case "$diagnostics_test_case" in
        collection_failure) export DIAGNOSTICS_TEST_COLLECTION_STATUS=7 ;;
        success|other)
            export DIAGNOSTICS_TEST_RESULT=0
            export DIAGNOSTICS_TEST_CRASH=$diagnostics_test_case
            diagnostics_test_expected_status=0
            ;;
        native|java|stream|restart|kernel)
            export DIAGNOSTICS_TEST_RESULT=0
            export DIAGNOSTICS_TEST_CRASH=$diagnostics_test_case
            diagnostics_test_expected_status=1
            ;;
        stale_emulator|stale_image)
            export DIAGNOSTICS_TEST_RESULT=0
            diagnostics_test_expected_status=2
            if [ "$diagnostics_test_case" = stale_emulator ]; then
                export DIAGNOSTICS_TEST_EMULATOR_VERSION=37.1.11
            else
                printf 'Pkg.Revision=4\n' > "$ANDROID_SDK_ROOT/system-images/android-35/google_apis_ps16k/x86_64/source.properties"
            fi
            ;;
        boot_device|boot_query)
            export DIAGNOSTICS_TEST_RESULT=0
            export DIAGNOSTICS_TEST_BOOT=${diagnostics_test_case#boot_}
            export ANDROID_TEST_BOOT_TIMEOUT_SECONDS=1
            diagnostics_test_expected_status=1
            ;;
        runtime_timeout)
            export DIAGNOSTICS_TEST_RESULT=0
            export DIAGNOSTICS_TEST_BOOT=runtime
            export ANDROID_TEST_RUNTIME_TIMEOUT_SECONDS=1
            diagnostics_test_expected_status=124
            ;;
        stuck_logcat|stuck_logcat_failure)
            export DIAGNOSTICS_TEST_LOGCAT_HANG=1
            if [ "$diagnostics_test_case" = stuck_logcat ]; then
                export DIAGNOSTICS_TEST_RESULT=0
                diagnostics_test_expected_status=0
            fi
            ;;
    esac
    mkdir -p "$DIAGNOSTICS_TEST_CASE"
    mkfifo "$DIAGNOSTICS_TEST_CASE/ready" "$DIAGNOSTICS_TEST_CASE/stop" \
        "$DIAGNOSTICS_TEST_CASE/logcat-ready" "$DIAGNOSTICS_TEST_CASE/boot-blocked"
    diagnostics_test_status=0
    timeout --kill-after=5s 30s sh "$diagnostics_test_runner" 35 google_apis_ps16k 16384 \
        > "$DIAGNOSTICS_TEST_CASE/output.log" 2>&1 || diagnostics_test_status=$?

    if [ "$diagnostics_test_status" -ne "$diagnostics_test_expected_status" ]; then
        cat "$DIAGNOSTICS_TEST_CASE/output.log"
        echo "FAIL: $diagnostics_test_case changed the test exit status" >&2
        exit 1
    fi
    if [ "$diagnostics_test_expected_status" -eq 2 ]; then
        test ! -f "$DIAGNOSTICS_TEST_CASE/started"
        test ! -f "$DIAGNOSTICS_TEST_CASE/test-completed"
        grep -Fq 'revision mismatch' "$DIAGNOSTICS_TEST_CASE/output.log"
        echo "android_diagnostics_test ($diagnostics_test_case): PASS"
        continue
    fi
    test -f "$DIAGNOSTICS_TEST_CASE/stopped"
    if [ -f "$DIAGNOSTICS_TEST_CASE/logcat.pid" ]; then
        if kill -0 "$(cat "$DIAGNOSTICS_TEST_CASE/logcat.pid")" 2>/dev/null; then
            echo "FAIL: $diagnostics_test_case left the log collector running" >&2
            exit 1
        fi
    fi
    diagnostics_test_saved=("$ANDROID_TEST_DIAGNOSTICS_DIR"/run.*)
    test "${#diagnostics_test_saved[@]}" -eq 1
    diagnostics_test_dir=${diagnostics_test_saved[0]}
    grep -Fq "test exit status: $diagnostics_test_expected_status" "$diagnostics_test_dir/collection-status.txt"
    if [ ! -f "$diagnostics_test_dir/emulator.log" ]; then
        echo "FAIL: $diagnostics_test_case lost emulator diagnostics during cleanup" >&2
        exit 1
    fi
    grep -Fq 'emulator diagnostic output' "$diagnostics_test_dir/emulator.log"
    if [ "$diagnostics_test_case" != boot_device ]; then
        test -f "$diagnostics_test_dir/logcat.log"
    fi
    if [ "$diagnostics_test_expected_status" -ne 0 ]; then
        grep -Fq 'test crash' "$diagnostics_test_dir/logcat-snapshot.log"
        grep -Fq '[ro.build.version.sdk]: [35]' "$diagnostics_test_dir/getprop.txt"
        grep -Fq 'system_server_crash: test stack trace' "$diagnostics_test_dir/dropbox.txt"
        grep -Fq 'test bugreport' "$diagnostics_test_dir/bugreport.zip"
        grep -Fq "bugreport.log: exit $DIAGNOSTICS_TEST_COLLECTION_STATUS" \
            "$diagnostics_test_dir/collection-status.txt"
    else
        test ! -f "$diagnostics_test_dir/bugreport.zip"
    fi
    echo "android_diagnostics_test ($diagnostics_test_case): PASS"
done
