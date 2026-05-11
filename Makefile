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

.PHONY: all test test-shared test-win32 test-win32-shared clean

all: $(TEST_BINS) $(NO_EXCEPTIONS_TEST_BIN) $(NO_POSIX_TEST_BIN) $(SUPPLEMENTAL_DISABLED_TEST_BIN) $(PRIMITIVES_DISABLED_TEST_BIN) $(IO_URING_TEST_BINS) $(GLIB_TEST_BINS) $(GIO_TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(WIN32_BUILD_DIR):
	mkdir -p $(WIN32_BUILD_DIR)

$(SHARED_BUILD_DIR):
	mkdir -p $(SHARED_BUILD_DIR)

$(WIN32_SHARED_BUILD_DIR):
	mkdir -p $(WIN32_SHARED_BUILD_DIR)

$(BUILD_DIR)/%: tests/%.cpp include/cardio.h tests/test_helpers.h | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(WIN32_BUILD_DIR)/%.exe: tests/%.cpp include/cardio.h tests/test_helpers.h Makefile | $(WIN32_BUILD_DIR)
	$(WIN32_CXX) $(CXXOPT) $(WIN32_CXXFLAGS) $< -o $@ $(WIN32_LDFLAGS) $(WIN32_LDLIBS)

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

clean:
	rm -rf $(BUILD_DIR)
