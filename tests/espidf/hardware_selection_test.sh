#!/usr/bin/env bash
set -eu
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export PATH="$dir:$PATH" CALLS="$dir/calls"
cat > "$dir/esptool" <<'EOF'
#!/bin/sh
[ "$*" = "--chip esp32s3 --port /dev/test-board read-mac" ] || exit 2
[ "$CHIP" = esp32s3 ] || exit 1
echo 'MAC: 12:34:56:ab:cd:ef'
EOF
cat > "$dir/pio" <<'EOF'
#!/bin/sh
echo "$*" >> "$CALLS"
EOF
chmod +x "$dir/esptool" "$dir/pio"
script=tests/espidf/run-hardware.sh
unset PORT EXPECT_MAC TEST_PORT
export CHIP=esp32s3
if bash "$script" > /dev/null 2>&1; then exit 1; fi
export PORT=/dev/test-board
if CHIP=esp32c3 bash "$script" > /dev/null 2>&1; then exit 1; fi
if EXPECT_MAC=00:00:00:00:00:00 bash "$script" > /dev/null 2>&1; then exit 1; fi
test ! -e "$CALLS"
EXPECT_MAC=12:34:56:AB:CD:EF TEST_PORT=/dev/test-console bash "$script" > /dev/null
grep -Fqx 'test -d samples/espidf -e esp32s3 --without-building --upload-port /dev/test-board --test-port /dev/test-console' "$CALLS"
echo 'ESP32-S3 hardware selection: PASS'
