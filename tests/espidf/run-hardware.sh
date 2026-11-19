#!/usr/bin/env bash
set -eu

: "${PORT:?Set PORT to the exact serial port of the ESP32-S3}"
case "$PORT" in
  *'*'*|*'?'*|*'['*) echo "PORT must identify one serial port" >&2; exit 1 ;;
esac
# esptool 5 checks the detected chip against --chip before reading the MAC.
identity=$(esptool --chip esp32s3 --port "$PORT" read-mac)
printf '%s\n' "$identity"
if [[ -n ${EXPECT_MAC:-} ]]; then
  if [[ ! $identity =~ MAC:[[:blank:]]*(([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}) ]]; then
    echo "Could not read the factory MAC on $PORT" >&2
    exit 1
  fi
  actual=${BASH_REMATCH[1]}
  if [[ ${actual,,} != ${EXPECT_MAC,,} ]]; then
    echo "Expected $EXPECT_MAC on $PORT, found $actual" >&2
    exit 1
  fi
fi
pio test -d samples/espidf -e esp32s3 --without-uploading --without-testing
exec timeout 180 pio test -d samples/espidf -e esp32s3 --without-building \
  --upload-port "$PORT" --test-port "${TEST_PORT:-$PORT}"
