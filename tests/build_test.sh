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
