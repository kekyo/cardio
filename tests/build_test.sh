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
