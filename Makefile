CXX ?= g++
CXXOPT ?= -O3 -g
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -pthread -Iinclude
PKG_CONFIG ?= pkg-config
LDLIBS ?=
WIN32_CXX ?= x86_64-w64-mingw32-g++
WINE ?= wine
WIN32_CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Iinclude
WIN32_LDFLAGS ?= -static-libgcc -static-libstdc++
WIN32_LDLIBS ?= -luser32
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 2>/dev/null)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 2>/dev/null)
GIO_AVAILABLE := $(shell $(PKG_CONFIG) --exists "gio-2.0 >= 2.44" 2>/dev/null && echo 1)
GIO_CFLAGS := $(shell $(PKG_CONFIG) --cflags "gio-2.0 >= 2.44" 2>/dev/null)
GIO_LIBS := $(shell $(PKG_CONFIG) --libs "gio-2.0 >= 2.44" 2>/dev/null)
ANDROID_NDK_ROOT ?=
ANDROID_SDK_ROOT ?=
ANDROID_API ?= 24
ANDROID_HOST_TAG ?= linux-x86_64
ANDROID_BUILD_TOOLS_VERSION ?= 36.0.0
ANDROID_PLATFORM_VERSION ?= 37.0
ANDROID_TOOLCHAIN := $(ANDROID_NDK_ROOT)/toolchains/llvm/prebuilt/$(ANDROID_HOST_TAG)/bin
ANDROID_BUILD_TOOLS := $(ANDROID_SDK_ROOT)/build-tools/$(ANDROID_BUILD_TOOLS_VERSION)
ANDROID_PLATFORM_JAR := $(ANDROID_SDK_ROOT)/platforms/android-$(ANDROID_PLATFORM_VERSION)/android.jar
ANDROID_X86_64_CXX ?= $(ANDROID_TOOLCHAIN)/x86_64-linux-android$(ANDROID_API)-clang++
ANDROID_ARM64_CXX ?= $(ANDROID_TOOLCHAIN)/aarch64-linux-android$(ANDROID_API)-clang++
ANDROID_CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -pthread -Iinclude -Itests
ANDROID_LDFLAGS ?= -static-libstdc++ -Wl,-z,max-page-size=16384
ANDROID_LDLIBS ?= -landroid
ANDROID_AAPT2 ?= $(ANDROID_BUILD_TOOLS)/aapt2
ANDROID_D8 ?= $(ANDROID_BUILD_TOOLS)/d8
ANDROID_ZIPALIGN ?= $(ANDROID_BUILD_TOOLS)/zipalign
ANDROID_APKSIGNER ?= $(ANDROID_BUILD_TOOLS)/apksigner
JAVAC ?= javac
KEYTOOL ?= keytool
ZIP ?= zip
ADB ?= adb
ADB_SERIAL ?=
ANDROID_RUNTIME_ABI ?= x86_64
ANDROID_EXPECTED_API ?=
ANDROID_EXPECTED_PAGE_SIZE ?=
ANDROID_DEVICE_TEST_DIR ?= /data/local/tmp/cardio-tests
ANDROID_ADB := $(ADB) $(if $(strip $(ADB_SERIAL)),-s $(ADB_SERIAL),)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
LIBURING_CFLAGS := $(shell $(PKG_CONFIG) --cflags liburing 2>/dev/null)
LIBURING_LIBS := $(shell $(PKG_CONFIG) --libs liburing 2>/dev/null)
ifeq ($(strip $(LIBURING_LIBS)),)
LIBURING_LIBS := -luring
endif
endif

BUILD_DIR := build
WIN32_BUILD_DIR := $(BUILD_DIR)/win32
ANDROID_BUILD_DIR := $(BUILD_DIR)/android
ANDROID_X86_64_BUILD_DIR := $(ANDROID_BUILD_DIR)/x86_64
ANDROID_ARM64_BUILD_DIR := $(ANDROID_BUILD_DIR)/arm64-v8a
ANDROID_CONFIG_TEST_SRC := tests/android/cardio_android_config_test.cpp
ANDROID_POSIX_DISABLED_TEST_SRC := tests/android/cardio_android_posix_disabled_test.cpp
ANDROID_IO_URING_TEST_SRC := tests/android/cardio_android_io_uring_test.cpp
ANDROID_MANUAL_TEST_SRC := tests/android/cardio_android_manual_test.cpp
ANDROID_AUTO_TEST_SRC := tests/android/cardio_android_auto_test.cpp
ANDROID_APP_SRC_DIR := tests/android/app
ANDROID_APP_MANIFEST := $(ANDROID_APP_SRC_DIR)/AndroidManifest.xml
ANDROID_APP_JAVA_SRCS := $(ANDROID_APP_SRC_DIR)/src/com/example/cardio/CardioActivity.java $(ANDROID_APP_SRC_DIR)/src/com/example/cardio/CardioInstrumentation.java
ANDROID_APP_BUILD_DIR := $(ANDROID_BUILD_DIR)/app
ANDROID_APP_CLASSES_DIR := $(ANDROID_APP_BUILD_DIR)/classes
ANDROID_APP_CLASSES_STAMP := $(ANDROID_APP_CLASSES_DIR)/.stamp
ANDROID_APP_DEX_DIR := $(ANDROID_APP_BUILD_DIR)/dex
ANDROID_APP_DEX := $(ANDROID_APP_DEX_DIR)/classes.dex
ANDROID_APP_KEYSTORE := $(ANDROID_APP_BUILD_DIR)/debug.keystore
ANDROID_X86_64_CONFIG_TEST_OBJ := $(ANDROID_X86_64_BUILD_DIR)/cardio_android_config_test.o
ANDROID_ARM64_CONFIG_TEST_OBJ := $(ANDROID_ARM64_BUILD_DIR)/cardio_android_config_test.o
ANDROID_X86_64_MANUAL_TEST_BIN := $(ANDROID_X86_64_BUILD_DIR)/cardio_android_manual_test
ANDROID_ARM64_MANUAL_TEST_BIN := $(ANDROID_ARM64_BUILD_DIR)/cardio_android_manual_test
ANDROID_X86_64_AUTO_TEST_LIB := $(ANDROID_X86_64_BUILD_DIR)/libcardio_android_auto_test.so
ANDROID_ARM64_AUTO_TEST_LIB := $(ANDROID_ARM64_BUILD_DIR)/libcardio_android_auto_test.so
ANDROID_X86_64_AUTO_TEST_APK := $(ANDROID_X86_64_BUILD_DIR)/cardio_android_auto_test.apk
ANDROID_ARM64_AUTO_TEST_APK := $(ANDROID_ARM64_BUILD_DIR)/cardio_android_auto_test.apk
ifeq ($(ANDROID_RUNTIME_ABI),x86_64)
ANDROID_RUNTIME_MANUAL_TEST_BIN := $(ANDROID_X86_64_MANUAL_TEST_BIN)
ANDROID_RUNTIME_AUTO_TEST_APK := $(ANDROID_X86_64_AUTO_TEST_APK)
else ifeq ($(ANDROID_RUNTIME_ABI),arm64-v8a)
ANDROID_RUNTIME_MANUAL_TEST_BIN := $(ANDROID_ARM64_MANUAL_TEST_BIN)
ANDROID_RUNTIME_AUTO_TEST_APK := $(ANDROID_ARM64_AUTO_TEST_APK)
else
$(error Unsupported ANDROID_RUNTIME_ABI: $(ANDROID_RUNTIME_ABI))
endif
NO_EXCEPTIONS_TEST_SRC := tests/cardio_no_exceptions_test.cpp
NO_EXCEPTIONS_TEST_BIN := $(BUILD_DIR)/cardio_no_exceptions_test_no_exceptions
NO_EXCEPTIONS_CXXFLAGS := $(CXXFLAGS) -fno-exceptions
NO_POSIX_TEST_SRC := tests/cardio_no_posix_test.cpp
NO_POSIX_TEST_BIN := $(BUILD_DIR)/cardio_no_posix_test_no_posix
NO_POSIX_CXXFLAGS := $(CXXFLAGS) -DCARDIO_HAS_POSIX_FD=0 -DCARDIO_WITH_LINUX_IO_URING=0
SUPPLEMENTAL_DISABLED_TEST_SRC := tests/cardio_supplemental_disabled_test.cpp
SUPPLEMENTAL_DISABLED_TEST_BIN := $(BUILD_DIR)/cardio_supplemental_disabled_test_no_supplemental
SUPPLEMENTAL_DISABLED_CXXFLAGS := $(CXXFLAGS) -DCARDIO_WITH_SUPPLEMENTAL=0
PRIMITIVES_DISABLED_TEST_SRC := tests/cardio_primitives_disabled_test.cpp
PRIMITIVES_DISABLED_TEST_BIN := $(BUILD_DIR)/cardio_primitives_disabled_test_no_primitives
PRIMITIVES_DISABLED_CXXFLAGS := $(CXXFLAGS) -DCARDIO_WITH_PRIMITIVES=0
IO_URING_TEST_SRC := tests/cardio_io_uring_test.cpp
IO_URING_TEST_BIN := $(BUILD_DIR)/cardio_io_uring_test
IO_URING_CXXFLAGS := $(CXXFLAGS) $(LIBURING_CFLAGS) -DCARDIO_WITH_LINUX_IO_URING=1
IO_URING_LDLIBS := $(LDLIBS) $(LIBURING_LIBS)
GLIB_TEST_SRC := tests/cardio_glib_test.cpp
GLIB_TEST_BIN := $(BUILD_DIR)/cardio_glib_test
GIO_TEST_SRC := tests/cardio_gio_test.cpp
GIO_TEST_BIN := $(BUILD_DIR)/cardio_gio_test
SHARED_TEST_SRC := tests/cardio_shared_host_test.cpp
SHARED_PLUGIN_SRC := tests/cardio_shared_plugin.cpp
SHARED_BUILD_DIR := $(BUILD_DIR)/shared
SHARED_LIBCARDIO := $(SHARED_BUILD_DIR)/libcardio.so
SHARED_PLUGIN := $(SHARED_BUILD_DIR)/cardio_shared_plugin.so
SHARED_TEST_BIN := $(SHARED_BUILD_DIR)/cardio_shared_host_test
SHARED_CXXFLAGS := $(CXXFLAGS) -DCARDIO_SHARED_LIB=1
WIN32_SHARED_BUILD_DIR := $(WIN32_BUILD_DIR)/shared
WIN32_SHARED_LIBCARDIO := $(WIN32_SHARED_BUILD_DIR)/libcardio.dll
WIN32_SHARED_IMPLIB := $(WIN32_SHARED_BUILD_DIR)/libcardio.dll.a
WIN32_SHARED_PLUGIN := $(WIN32_SHARED_BUILD_DIR)/cardio_shared_plugin.dll
WIN32_SHARED_TEST_BIN := $(WIN32_SHARED_BUILD_DIR)/cardio_shared_host_test.exe
WIN32_SHARED_CXXFLAGS := $(WIN32_CXXFLAGS) -DCARDIO_SHARED_LIB=1
ifeq ($(strip $(GLIB_LIBS)),)
GLIB_TEST_BINS :=
else
GLIB_TEST_BINS := $(GLIB_TEST_BIN)
endif
ifeq ($(GIO_AVAILABLE),1)
GIO_TEST_BINS := $(GIO_TEST_BIN)
else
GIO_TEST_BINS :=
endif
ifeq ($(UNAME_S),Linux)
IO_URING_TEST_BINS := $(IO_URING_TEST_BIN)
else
IO_URING_TEST_BINS :=
endif
TEST_SRCS := $(filter-out $(NO_EXCEPTIONS_TEST_SRC) $(NO_POSIX_TEST_SRC) $(SUPPLEMENTAL_DISABLED_TEST_SRC) $(PRIMITIVES_DISABLED_TEST_SRC) $(IO_URING_TEST_SRC) $(GLIB_TEST_SRC) $(GIO_TEST_SRC) $(SHARED_TEST_SRC),$(wildcard tests/*_test.cpp))
TEST_BINS := $(patsubst tests/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
WIN32_TEST_SRCS := $(filter-out $(NO_EXCEPTIONS_TEST_SRC) $(SUPPLEMENTAL_DISABLED_TEST_SRC) $(PRIMITIVES_DISABLED_TEST_SRC) $(IO_URING_TEST_SRC) $(GLIB_TEST_SRC) $(GIO_TEST_SRC) $(SHARED_TEST_SRC),$(wildcard tests/*_test.cpp))
WIN32_TEST_BINS := $(patsubst tests/%.cpp,$(WIN32_BUILD_DIR)/%.exe,$(WIN32_TEST_SRCS))
WIN32_NO_EXCEPTIONS_TEST_BIN := $(WIN32_BUILD_DIR)/cardio_no_exceptions_test_no_exceptions.exe
WIN32_NO_EXCEPTIONS_CXXFLAGS := $(WIN32_CXXFLAGS) -fno-exceptions
WIN32_SUPPLEMENTAL_DISABLED_TEST_BIN := $(WIN32_BUILD_DIR)/cardio_supplemental_disabled_test_no_supplemental.exe
WIN32_SUPPLEMENTAL_DISABLED_CXXFLAGS := $(WIN32_CXXFLAGS) -DCARDIO_WITH_SUPPLEMENTAL=0
WIN32_PRIMITIVES_DISABLED_TEST_BIN := $(WIN32_BUILD_DIR)/cardio_primitives_disabled_test_no_primitives.exe
WIN32_PRIMITIVES_DISABLED_CXXFLAGS := $(WIN32_CXXFLAGS) -DCARDIO_WITH_PRIMITIVES=0

.PHONY: all test test-shared test-win32 test-win32-shared test-android test-android-config test-android-runtime clean

all: $(TEST_BINS) $(NO_EXCEPTIONS_TEST_BIN) $(NO_POSIX_TEST_BIN) $(SUPPLEMENTAL_DISABLED_TEST_BIN) $(PRIMITIVES_DISABLED_TEST_BIN) $(IO_URING_TEST_BINS) $(GLIB_TEST_BINS) $(GIO_TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(WIN32_BUILD_DIR):
	mkdir -p $(WIN32_BUILD_DIR)

$(ANDROID_X86_64_BUILD_DIR):
	mkdir -p $(ANDROID_X86_64_BUILD_DIR)

$(ANDROID_ARM64_BUILD_DIR):
	mkdir -p $(ANDROID_ARM64_BUILD_DIR)

$(SHARED_BUILD_DIR):
	mkdir -p $(SHARED_BUILD_DIR)

$(WIN32_SHARED_BUILD_DIR):
	mkdir -p $(WIN32_SHARED_BUILD_DIR)

$(BUILD_DIR)/%: tests/%.cpp include/cardio.h tests/test_helpers.h | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(WIN32_BUILD_DIR)/%.exe: tests/%.cpp include/cardio.h tests/test_helpers.h Makefile | $(WIN32_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_CXXFLAGS) $< -o $@ $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

$(ANDROID_X86_64_CONFIG_TEST_OBJ): $(ANDROID_CONFIG_TEST_SRC) include/cardio.h Makefile | $(ANDROID_X86_64_BUILD_DIR)
	$(ANDROID_X86_64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -c $< -o $@

$(ANDROID_ARM64_CONFIG_TEST_OBJ): $(ANDROID_CONFIG_TEST_SRC) include/cardio.h Makefile | $(ANDROID_ARM64_BUILD_DIR)
	$(ANDROID_ARM64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -c $< -o $@

$(ANDROID_X86_64_MANUAL_TEST_BIN): $(ANDROID_MANUAL_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(ANDROID_X86_64_BUILD_DIR)
	$(ANDROID_X86_64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) $< -o $@ $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

$(ANDROID_ARM64_MANUAL_TEST_BIN): $(ANDROID_MANUAL_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(ANDROID_ARM64_BUILD_DIR)
	$(ANDROID_ARM64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) $< -o $@ $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

$(ANDROID_X86_64_AUTO_TEST_LIB): $(ANDROID_AUTO_TEST_SRC) include/cardio.h Makefile | $(ANDROID_X86_64_BUILD_DIR)
	$(ANDROID_X86_64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -fPIC -shared $< -o $@ $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

$(ANDROID_ARM64_AUTO_TEST_LIB): $(ANDROID_AUTO_TEST_SRC) include/cardio.h Makefile | $(ANDROID_ARM64_BUILD_DIR)
	$(ANDROID_ARM64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -fPIC -shared $< -o $@ $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

$(ANDROID_APP_CLASSES_STAMP): $(ANDROID_APP_JAVA_SRCS) $(ANDROID_APP_MANIFEST) Makefile
	mkdir -p $(ANDROID_APP_CLASSES_DIR)
	$(JAVAC) --release 8 -Xlint:-options -classpath $(ANDROID_PLATFORM_JAR) -d $(ANDROID_APP_CLASSES_DIR) $(ANDROID_APP_JAVA_SRCS)
	touch $@

$(ANDROID_APP_DEX): $(ANDROID_APP_CLASSES_STAMP)
	mkdir -p $(ANDROID_APP_DEX_DIR)
	$(ANDROID_D8) --min-api 24 --lib $(ANDROID_PLATFORM_JAR) --output $(ANDROID_APP_DEX_DIR) $$(find $(ANDROID_APP_CLASSES_DIR) -name '*.class' -print)

$(ANDROID_APP_KEYSTORE):
	mkdir -p $(ANDROID_APP_BUILD_DIR)
	$(KEYTOOL) -genkeypair -noprompt -keystore $@ -storetype PKCS12 -storepass android -keypass android -alias androiddebugkey -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -validity 10000

$(ANDROID_X86_64_AUTO_TEST_APK): $(ANDROID_X86_64_AUTO_TEST_LIB) $(ANDROID_APP_DEX) $(ANDROID_APP_KEYSTORE) $(ANDROID_APP_MANIFEST) Makefile
	mkdir -p $(ANDROID_X86_64_BUILD_DIR)/app-staging/lib/x86_64
	$(ANDROID_AAPT2) link -I $(ANDROID_PLATFORM_JAR) --manifest $(ANDROID_APP_MANIFEST) -o $(ANDROID_X86_64_BUILD_DIR)/app-staging/base.apk
	cp $(ANDROID_APP_DEX) $(ANDROID_X86_64_BUILD_DIR)/app-staging/classes.dex
	cp $(ANDROID_X86_64_AUTO_TEST_LIB) $(ANDROID_X86_64_BUILD_DIR)/app-staging/lib/x86_64/libcardio_android_auto_test.so
	cd $(ANDROID_X86_64_BUILD_DIR)/app-staging && $(ZIP) -q -0 base.apk classes.dex lib/x86_64/libcardio_android_auto_test.so
	$(ANDROID_ZIPALIGN) -P 16 -f 4 $(ANDROID_X86_64_BUILD_DIR)/app-staging/base.apk $(ANDROID_X86_64_BUILD_DIR)/app-staging/aligned.apk
	$(ANDROID_APKSIGNER) sign --ks $(ANDROID_APP_KEYSTORE) --ks-pass pass:android --key-pass pass:android --out $@ $(ANDROID_X86_64_BUILD_DIR)/app-staging/aligned.apk
	$(ANDROID_APKSIGNER) verify $@

$(ANDROID_ARM64_AUTO_TEST_APK): $(ANDROID_ARM64_AUTO_TEST_LIB) $(ANDROID_APP_DEX) $(ANDROID_APP_KEYSTORE) $(ANDROID_APP_MANIFEST) Makefile
	mkdir -p $(ANDROID_ARM64_BUILD_DIR)/app-staging/lib/arm64-v8a
	$(ANDROID_AAPT2) link -I $(ANDROID_PLATFORM_JAR) --manifest $(ANDROID_APP_MANIFEST) -o $(ANDROID_ARM64_BUILD_DIR)/app-staging/base.apk
	cp $(ANDROID_APP_DEX) $(ANDROID_ARM64_BUILD_DIR)/app-staging/classes.dex
	cp $(ANDROID_ARM64_AUTO_TEST_LIB) $(ANDROID_ARM64_BUILD_DIR)/app-staging/lib/arm64-v8a/libcardio_android_auto_test.so
	cd $(ANDROID_ARM64_BUILD_DIR)/app-staging && $(ZIP) -q -0 base.apk classes.dex lib/arm64-v8a/libcardio_android_auto_test.so
	$(ANDROID_ZIPALIGN) -P 16 -f 4 $(ANDROID_ARM64_BUILD_DIR)/app-staging/base.apk $(ANDROID_ARM64_BUILD_DIR)/app-staging/aligned.apk
	$(ANDROID_APKSIGNER) sign --ks $(ANDROID_APP_KEYSTORE) --ks-pass pass:android --key-pass pass:android --out $@ $(ANDROID_ARM64_BUILD_DIR)/app-staging/aligned.apk
	$(ANDROID_APKSIGNER) verify $@

$(NO_EXCEPTIONS_TEST_BIN): $(NO_EXCEPTIONS_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(NO_EXCEPTIONS_CXXFLAGS) $< -o $@ $(LDLIBS)

$(NO_POSIX_TEST_BIN): $(NO_POSIX_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(NO_POSIX_CXXFLAGS) $< -o $@ $(LDLIBS)

$(SUPPLEMENTAL_DISABLED_TEST_BIN): $(SUPPLEMENTAL_DISABLED_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(SUPPLEMENTAL_DISABLED_CXXFLAGS) $< -o $@ $(LDLIBS)

$(PRIMITIVES_DISABLED_TEST_BIN): $(PRIMITIVES_DISABLED_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(PRIMITIVES_DISABLED_CXXFLAGS) $< -o $@ $(LDLIBS)

$(IO_URING_TEST_BIN): $(IO_URING_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(IO_URING_CXXFLAGS) $< -o $@ $(IO_URING_LDLIBS)

$(GLIB_TEST_BIN): $(GLIB_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) $(GLIB_CFLAGS) -DCARDIO_WITH_GLIB=1 $< -o $@ $(LDLIBS) $(GLIB_LIBS)

$(GIO_TEST_BIN): $(GIO_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) $(GIO_CFLAGS) -DCARDIO_WITH_GLIB=1 -DCARDIO_WITH_GIO=1 $< -o $@ $(LDLIBS) $(GIO_LIBS)

$(SHARED_LIBCARDIO): samples/libcardio/libcardio.cpp include/cardio.h samples/libcardio/Makefile.posix | $(SHARED_BUILD_DIR)
	$(MAKE) -C samples/libcardio -f Makefile.posix BUILD_DIR=../../$(SHARED_BUILD_DIR) CARDIO_INCLUDE_DIR=../../include CXX="$(CXX)" CXXOPT="$(CXXOPT)" CXXFLAGS="$(CXXFLAGS)" CARDIO_LDLIBS="$(LDLIBS)"

$(SHARED_PLUGIN): $(SHARED_PLUGIN_SRC) include/cardio.h $(SHARED_LIBCARDIO) Makefile | $(SHARED_BUILD_DIR)
	$(CXX) $(CXXOPT) $(SHARED_CXXFLAGS) -fPIC -shared $< -o $@ -L$(SHARED_BUILD_DIR) -lcardio -Wl,-rpath,'$$ORIGIN' $(LDLIBS)

$(SHARED_TEST_BIN): $(SHARED_TEST_SRC) include/cardio.h tests/test_helpers.h $(SHARED_LIBCARDIO) Makefile | $(SHARED_BUILD_DIR)
	$(CXX) $(CXXOPT) $(SHARED_CXXFLAGS) $< -o $@ -L$(SHARED_BUILD_DIR) -lcardio -ldl -Wl,-rpath,'$$ORIGIN' $(LDLIBS)

$(WIN32_NO_EXCEPTIONS_TEST_BIN): $(NO_EXCEPTIONS_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(WIN32_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_NO_EXCEPTIONS_CXXFLAGS) $< -o $@ $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

$(WIN32_SUPPLEMENTAL_DISABLED_TEST_BIN): $(SUPPLEMENTAL_DISABLED_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(WIN32_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_SUPPLEMENTAL_DISABLED_CXXFLAGS) $< -o $@ $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

$(WIN32_PRIMITIVES_DISABLED_TEST_BIN): $(PRIMITIVES_DISABLED_TEST_SRC) include/cardio.h tests/test_helpers.h Makefile | $(WIN32_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_PRIMITIVES_DISABLED_CXXFLAGS) $< -o $@ $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

$(WIN32_SHARED_LIBCARDIO) $(WIN32_SHARED_IMPLIB): samples/libcardio/libcardio.cpp include/cardio.h samples/libcardio/Makefile.win32 | $(WIN32_SHARED_BUILD_DIR)
	$(MAKE) -C samples/libcardio -f Makefile.win32 WIN32_BUILD_DIR=../../$(WIN32_SHARED_BUILD_DIR) CARDIO_INCLUDE_DIR=../../include WIN32_CXX="$(WIN32_CXX)" CXXOPT="$(CXXOPT)" WIN32_CXXFLAGS="$(WIN32_CXXFLAGS)" WIN32_LDFLAGS="$(WIN32_LDFLAGS)" WIN32_LDLIBS="$(WIN32_LDLIBS)"

$(WIN32_SHARED_PLUGIN): $(SHARED_PLUGIN_SRC) include/cardio.h $(WIN32_SHARED_IMPLIB) Makefile | $(WIN32_SHARED_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_SHARED_CXXFLAGS) -shared $< -o $@ -L$(WIN32_SHARED_BUILD_DIR) -lcardio $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

$(WIN32_SHARED_TEST_BIN): $(SHARED_TEST_SRC) include/cardio.h tests/test_helpers.h $(WIN32_SHARED_IMPLIB) Makefile | $(WIN32_SHARED_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_SHARED_CXXFLAGS) $< -o $@ -L$(WIN32_SHARED_BUILD_DIR) -lcardio $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

ifeq ($(UNAME_S),Linux)
test-shared: $(SHARED_TEST_BIN) $(SHARED_PLUGIN)
	$(SHARED_TEST_BIN) $(SHARED_PLUGIN)
else
test-shared:
endif

test: $(TEST_BINS) $(NO_EXCEPTIONS_TEST_BIN) $(NO_POSIX_TEST_BIN) $(SUPPLEMENTAL_DISABLED_TEST_BIN) $(PRIMITIVES_DISABLED_TEST_BIN) $(IO_URING_TEST_BINS) $(GLIB_TEST_BINS) $(GIO_TEST_BINS) test-shared
	for test_bin in $(TEST_BINS) $(NO_EXCEPTIONS_TEST_BIN) $(NO_POSIX_TEST_BIN) $(SUPPLEMENTAL_DISABLED_TEST_BIN) $(PRIMITIVES_DISABLED_TEST_BIN) $(IO_URING_TEST_BINS) $(GLIB_TEST_BINS) $(GIO_TEST_BINS); do $$test_bin || exit $$?; done

test-win32-shared: $(WIN32_SHARED_TEST_BIN) $(WIN32_SHARED_PLUGIN) $(WIN32_SHARED_LIBCARDIO)
	cd $(WIN32_SHARED_BUILD_DIR) && WINEPATH=. $(WINE) ./cardio_shared_host_test.exe cardio_shared_plugin.dll

test-win32: $(WIN32_TEST_BINS) $(WIN32_NO_EXCEPTIONS_TEST_BIN) $(WIN32_SUPPLEMENTAL_DISABLED_TEST_BIN) $(WIN32_PRIMITIVES_DISABLED_TEST_BIN) test-win32-shared
	for test_bin in $(WIN32_TEST_BINS) $(WIN32_NO_EXCEPTIONS_TEST_BIN) $(WIN32_SUPPLEMENTAL_DISABLED_TEST_BIN) $(WIN32_PRIMITIVES_DISABLED_TEST_BIN); do $(WINE) $$test_bin || exit $$?; done

test-android-config: $(ANDROID_X86_64_CONFIG_TEST_OBJ) $(ANDROID_ARM64_CONFIG_TEST_OBJ)
	@output="$$( $(ANDROID_X86_64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -DCARDIO_HAS_POSIX_FD=0 -fsyntax-only $(ANDROID_POSIX_DISABLED_TEST_SRC) 2>&1 )"; status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "cardio_android_posix_disabled_test (x86_64): FAIL"; exit 1; \
	elif printf '%s\n' "$$output" | grep -Fq "Android requires CARDIO_HAS_POSIX_FD"; then \
		echo "cardio_android_posix_disabled_test (x86_64): PASS"; \
	else \
		printf '%s\n' "$$output"; echo "cardio_android_posix_disabled_test (x86_64): FAIL"; exit 1; \
	fi
	@output="$$( $(ANDROID_ARM64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -DCARDIO_HAS_POSIX_FD=0 -fsyntax-only $(ANDROID_POSIX_DISABLED_TEST_SRC) 2>&1 )"; status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "cardio_android_posix_disabled_test (arm64-v8a): FAIL"; exit 1; \
	elif printf '%s\n' "$$output" | grep -Fq "Android requires CARDIO_HAS_POSIX_FD"; then \
		echo "cardio_android_posix_disabled_test (arm64-v8a): PASS"; \
	else \
		printf '%s\n' "$$output"; echo "cardio_android_posix_disabled_test (arm64-v8a): FAIL"; exit 1; \
	fi
	@output="$$( $(ANDROID_X86_64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -DCARDIO_WITH_LINUX_IO_URING=1 -fsyntax-only $(ANDROID_IO_URING_TEST_SRC) 2>&1 )"; status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "cardio_android_io_uring_test (x86_64): FAIL"; exit 1; \
	elif printf '%s\n' "$$output" | grep -Fq "CARDIO_WITH_LINUX_IO_URING is not supported on Android"; then \
		echo "cardio_android_io_uring_test (x86_64): PASS"; \
	else \
		printf '%s\n' "$$output"; echo "cardio_android_io_uring_test (x86_64): FAIL"; exit 1; \
	fi
	@output="$$( $(ANDROID_ARM64_CXX) $(CXXOPT) $(ANDROID_CXXFLAGS) -DCARDIO_WITH_LINUX_IO_URING=1 -fsyntax-only $(ANDROID_IO_URING_TEST_SRC) 2>&1 )"; status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "cardio_android_io_uring_test (arm64-v8a): FAIL"; exit 1; \
	elif printf '%s\n' "$$output" | grep -Fq "CARDIO_WITH_LINUX_IO_URING is not supported on Android"; then \
		echo "cardio_android_io_uring_test (arm64-v8a): PASS"; \
	else \
		printf '%s\n' "$$output"; echo "cardio_android_io_uring_test (arm64-v8a): FAIL"; exit 1; \
	fi

test-android: test-android-config $(ANDROID_X86_64_MANUAL_TEST_BIN) $(ANDROID_ARM64_MANUAL_TEST_BIN) $(ANDROID_X86_64_AUTO_TEST_APK) $(ANDROID_ARM64_AUTO_TEST_APK)

test-android-runtime: test-android $(ANDROID_RUNTIME_MANUAL_TEST_BIN)
	@if [ -z "$(ANDROID_EXPECTED_API)" ]; then \
		echo "ANDROID_EXPECTED_API is required"; exit 1; \
	fi
	@if [ -z "$(ANDROID_EXPECTED_PAGE_SIZE)" ]; then \
		echo "ANDROID_EXPECTED_PAGE_SIZE is required"; exit 1; \
	fi
	@actual_abi="$$( $(ANDROID_ADB) shell getprop ro.product.cpu.abi | tr -d '\r' )"; \
	if [ "$$actual_abi" != "$(ANDROID_RUNTIME_ABI)" ]; then \
		echo "Android ABI mismatch: expected $(ANDROID_RUNTIME_ABI), got $$actual_abi"; exit 1; \
	fi
	@actual_api="$$( $(ANDROID_ADB) shell getprop ro.build.version.sdk | tr -d '\r' )"; \
	if [ "$$actual_api" != "$(ANDROID_EXPECTED_API)" ]; then \
		echo "Android API mismatch: expected $(ANDROID_EXPECTED_API), got $$actual_api"; exit 1; \
	fi
	@actual_page_size="$$( $(ANDROID_ADB) shell getconf PAGE_SIZE | tr -d '\r' )"; \
	if [ "$$actual_page_size" != "$(ANDROID_EXPECTED_PAGE_SIZE)" ]; then \
		echo "Android page size mismatch: expected $(ANDROID_EXPECTED_PAGE_SIZE), got $$actual_page_size"; exit 1; \
	fi
	$(ANDROID_ADB) shell mkdir -p $(ANDROID_DEVICE_TEST_DIR)
	$(ANDROID_ADB) push $(ANDROID_RUNTIME_MANUAL_TEST_BIN) $(ANDROID_DEVICE_TEST_DIR)/cardio_android_manual_test
	$(ANDROID_ADB) shell chmod 755 $(ANDROID_DEVICE_TEST_DIR)/cardio_android_manual_test
	$(ANDROID_ADB) shell timeout -k 5s 60s $(ANDROID_DEVICE_TEST_DIR)/cardio_android_manual_test
	$(ANDROID_ADB) install -r $(ANDROID_RUNTIME_AUTO_TEST_APK)
	@output="$$( $(ANDROID_ADB) shell am instrument -w com.example.cardio/.CardioInstrumentation 2>&1 )"; status=$$?; \
	printf '%s\n' "$$output"; \
	if [ $$status -ne 0 ] || ! printf '%s\n' "$$output" | grep -Fq "cardio_android_auto_test: PASS"; then \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR)
