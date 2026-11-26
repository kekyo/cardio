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
xcrun simctl launch --console "$device" com.example.cardio.runloop-test > "$output/launch.log" 2>&1 || {
    cat "$output/launch.log" >&2
    exit 1
}
cat "$output/launch.log"
grep -Fxq 'cardio_ios_cfrunloop_test: PASS' "$output/launch.log"
