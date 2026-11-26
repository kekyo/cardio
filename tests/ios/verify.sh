#!/bin/sh
set -eu

output=$1
minimum=15.0
simulator_arch=$(uname -m)
for sdk in iphoneos iphonesimulator; do
    target="arm64-apple-ios$minimum"
    if [ "$sdk" = iphonesimulator ]; then target="$simulator_arch-apple-ios$minimum-simulator"; fi
    mkdir -p "$output/$sdk"
    echo "$sdk $(xcrun --sdk "$sdk" --show-sdk-version), target $target"
    compiler() {
        xcrun --sdk "$sdk" clang++ -target "$target" -std=c++20 -Wall -Wextra -Wpedantic -Iinclude "$@"
    }
    compiler tests/cardio_fd_test.cpp -o "$output/$sdk/poll"
    compiler -DCARDIO_TEST_KQUEUE tests/cardio_fd_test.cpp -o "$output/$sdk/kqueue"
    compiler -DCARDIO_WITH_CFRUNLOOP=1 tests/macos/cardio_cfrunloop_test.cpp \
        -framework CoreFoundation -o "$output/$sdk/cfrunloop"
    compiler -DCARDIO_WITH_CFRUNLOOP=1 tests/cardio_host_selection_test.cpp \
        -framework CoreFoundation -o "$output/$sdk/host-selection"
    echo "$sdk compile and link: PASS"
done

app="$output/CardioRunLoopTest.app"
mkdir -p "$app"
cp tests/ios/Info.plist "$app/Info.plist"
xcrun --sdk iphonesimulator clang++ -target "$simulator_arch-apple-ios$minimum-simulator" \
    -std=c++20 -Wall -Wextra -Wpedantic -fobjc-arc -Iinclude -DCARDIO_WITH_CFRUNLOOP=1 \
    tests/ios/cardio_runloop_app.mm -framework UIKit -framework Foundation -framework CoreFoundation \
    -o "$app/cardio_runloop_test"
codesign --force --sign - "$app"

runtime=com.apple.CoreSimulator.SimRuntime.iOS-18-5
device=$(xcrun simctl create CardioRunLoopTest com.apple.CoreSimulator.SimDeviceType.iPhone-16 "$runtime")
trap 'xcrun simctl shutdown "$device" >/dev/null 2>&1 || true; xcrun simctl delete "$device"' EXIT
xcrun simctl bootstatus "$device" -b
xcrun simctl install "$device" "$app"
bundle=com.example.cardio.runloop-test
data=$(xcrun simctl get_app_container "$device" "$bundle" data)
# Keep the app's result even if simctl forwards only its launch PID.
report=$(mktemp "$data/tmp/cardio-runloop.XXXXXX")
status=0
SIMCTL_CHILD_CARDIO_TEST_REPORT="$report" xcrun simctl launch --console "$device" "$bundle" \
    > "$output/launch.log" 2>&1 || status=$?
cat "$output/launch.log"
cp "$report" "$output/app.log"
cat "$output/app.log"
if [ "$status" -eq 0 ] && grep -Fxq 'cardio_ios_cfrunloop_test: PASS' "$output/app.log"; then
    exit 0
fi

echo "UIKit test did not complete (simctl launch status $status)" >&2
xcrun simctl spawn "$device" log show --last 5m --style compact --info \
    --predicate 'process == "cardio_runloop_test" OR eventMessage CONTAINS[c] "com.example.cardio.runloop-test"' >&2 || true
if [ "$status" -ne 0 ]; then exit "$status"; fi
exit 1
