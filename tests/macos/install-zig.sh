#!/bin/sh
set -eu

destination=$1
version=$2
checksum=$3
parent=$(dirname "$destination")
mkdir -p "$parent"
staging=$(mktemp -d "$parent/.install-XXXXXX")
trap 'rm -rf "$staging"' EXIT
trap 'exit 1' HUP INT TERM

archive="zig-x86_64-linux-$version"
curl --fail --location --silent --show-error --retry 3 \
    "https://ziglang.org/download/$version/$archive.tar.xz" -o "$staging/archive.tar.xz"
printf '%s  %s\n' "$checksum" "$staging/archive.tar.xz" | sha256sum -c -
tar -xJf "$staging/archive.tar.xz" -C "$staging"
test -x "$staging/$archive/zig"
# A separate make process may have finished installing this version first.
mv -T "$staging/$archive" "$destination" || test -x "$destination/zig"
