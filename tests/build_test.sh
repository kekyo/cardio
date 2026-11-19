#!/bin/sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
unset MAKEFLAGS MFLAGS

export ANDROID_SDK_ROOT="$work/sdk" ANDROID_NDK_ROOT="$work/ndk"
ndk_bin="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
mkdir -p "$ndk_bin"
ln -s /bin/true "$ndk_bin/x86_64-linux-android24-clang++"

cat > Makefile <<'EOF'
test test-win32 test-android:
	@echo '$(filter -j%,$(MAKEFLAGS))' > $@
EOF
"$root/build.sh"
for target in test test-win32 test-android; do
    jobs=$(cat "$target")
    case "$jobs" in
        ''|-j[1-4]) ;;
        *) echo "$target: expected at most four parallel jobs, got $jobs" >&2; exit 1 ;;
    esac
done
echo 'Build parallelism: PASS'

dll=build/win32/shared/libcardio.dll
make -C "$root" -B -n -j4 WIN32_CXX=link-probe "$dll" "$dll.a" > win32-commands
links=$(awk '$1 == "link-probe" { count++ } END { print count + 0 }' win32-commands)
if [ "$links" -ne 1 ]; then
    echo "Expected one link for the DLL and its import library, got $links" >&2
    exit 1
fi
echo 'Win32 shared library build: PASS'

# API 24 adbd can lose the exit status of a short command after it succeeds.
cat > adb <<'EOF'
#!/bin/sh
set -eu
case "$1" in
    shell)
        shift
        case "$*" in
            -T) exec sh ;;
            'getprop ro.product.cpu.abi') echo x86_64 ;;
            'getprop ro.build.version.sdk') echo 24 ;;
            'am instrument '*) echo 'cardio_android_auto_test: PASS' ;;
            *)
                output=$("$@")
                [ -n "$output" ] || exit 255
                printf '%s\n' "$output"
                ;;
        esac
        ;;
    push)
        mkdir -p "$(dirname "$3")"
        cp "$2" "$3"
        chmod 777 "$3"
        ;;
    install) test -f "$3" ;;
    *) exit 1 ;;
esac
EOF
cat > manual-test <<'EOF'
#!/bin/sh
set -eu
test -n "$(find "$0" -perm 0755)"
test "$1" = 4096
echo 'cardio_android_manual_test: PASS'
EOF
chmod +x adb manual-test
touch auto-test.apk
run_android_runtime() {
    make -C "$root" -o test-android test-android-runtime \
        ADB="$work/adb" ADB_SERIAL= ANDROID_RUNTIME_ABI=x86_64 \
        ANDROID_EXPECTED_API=24 ANDROID_EXPECTED_PAGE_SIZE="$1" \
        ANDROID_DEVICE_TEST_DIR="$work/device/cardio-tests" \
        ANDROID_RUNTIME_MANUAL_TEST_BIN="$work/manual-test" \
        ANDROID_RUNTIME_AUTO_TEST_APK="$work/auto-test.apk" > android-runtime.log 2>&1
}
if ! run_android_runtime 4096; then
    cat android-runtime.log >&2
    exit 1
fi
grep -Fxq 'cardio_android_manual_test: PASS' android-runtime.log
if run_android_runtime 16384; then
    echo 'Android runtime ignored a failing manual test' >&2
    exit 1
fi
echo 'Android executable deployment: PASS'
