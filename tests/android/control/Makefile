# Included by the root Makefile. The control APK has no native dependencies.
ANDROID_CONTROL_SRC_DIR := tests/android/control
ANDROID_CONTROL_BUILD_DIR := $(ANDROID_BUILD_DIR)/control
ANDROID_CONTROL_JAVA_SRCS := $(wildcard $(ANDROID_CONTROL_SRC_DIR)/src/com/example/cardio/control/*.java)
ANDROID_CONTROL_MANIFEST := $(ANDROID_CONTROL_SRC_DIR)/AndroidManifest.xml
ANDROID_CONTROL_APK := $(ANDROID_CONTROL_BUILD_DIR)/android_activity_control_test.apk

.PHONY: android-control-apk test-android-control test-android-control-recreation

android-control-apk: $(ANDROID_CONTROL_APK)

$(ANDROID_CONTROL_APK): $(ANDROID_CONTROL_JAVA_SRCS) $(ANDROID_CONTROL_MANIFEST) $(ANDROID_APP_KEYSTORE) tests/android/control/Makefile
	mkdir -p $(ANDROID_CONTROL_BUILD_DIR)/classes $(ANDROID_CONTROL_BUILD_DIR)/dex
	$(JAVAC) --release 8 -Xlint:-options -classpath $(ANDROID_PLATFORM_JAR) -d $(ANDROID_CONTROL_BUILD_DIR)/classes $(ANDROID_CONTROL_JAVA_SRCS)
	$(ANDROID_D8) --min-api 24 --lib $(ANDROID_PLATFORM_JAR) --output $(ANDROID_CONTROL_BUILD_DIR)/dex $$(find $(ANDROID_CONTROL_BUILD_DIR)/classes -name '*.class' -print)
	$(ANDROID_AAPT2) link -I $(ANDROID_PLATFORM_JAR) --manifest $(ANDROID_CONTROL_MANIFEST) -o $(ANDROID_CONTROL_BUILD_DIR)/dex/base.apk
	cd $(ANDROID_CONTROL_BUILD_DIR)/dex && $(ZIP) -q -0 base.apk classes.dex
	$(ANDROID_ZIPALIGN) -f 4 $(ANDROID_CONTROL_BUILD_DIR)/dex/base.apk $(ANDROID_CONTROL_BUILD_DIR)/aligned.apk
	$(ANDROID_APKSIGNER) sign --ks $(ANDROID_APP_KEYSTORE) --ks-pass pass:android --key-pass pass:android --out $@ $(ANDROID_CONTROL_BUILD_DIR)/aligned.apk
	$(ANDROID_APKSIGNER) verify $@

# The prerequisite installs the APK. On API 24, reinstalling it between tests can
# stop the next instrumentation through a delayed package-removal notification.
test-android-control: test-android-control-recreation $(ANDROID_CONTROL_APK)
	@output="$$( $(ANDROID_ADB) shell am instrument -w -e api $(ANDROID_EXPECTED_API) -e page_size $(ANDROID_EXPECTED_PAGE_SIZE) com.example.cardio.control/.ControlInstrumentation 2>&1 )"; status=$$?; \
	printf '%s\n' "$$output"; \
	if [ $$status -ne 0 ] || ! printf '%s\n' "$$output" | grep -Fq "android_activity_control_test: PASS"; then \
		exit 1; \
	fi

test-android-control-recreation: $(ANDROID_CONTROL_APK)
	$(ANDROID_ADB) install -r $(ANDROID_CONTROL_APK)
	@output="$$( $(ANDROID_ADB) shell am instrument -w -e api $(ANDROID_EXPECTED_API) -e page_size $(ANDROID_EXPECTED_PAGE_SIZE) -e recreate true com.example.cardio.control/.ControlInstrumentation 2>&1 )"; status=$$?; \
	printf '%s\n' "$$output"; \
	if [ $$status -ne 0 ] || ! printf '%s\n' "$$output" | grep -Fq "android_activity_control_recreation_test: PASS"; then \
		exit 1; \
	fi
