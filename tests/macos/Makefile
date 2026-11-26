ZIG_VERSION := 0.15.2
ZIG_DIR := $(BUILD_DIR)/tools/zig/$(ZIG_VERSION)/x86_64-linux
ZIG := $(ZIG_DIR)/zig
MACOS_CROSS_DIR := $(BUILD_DIR)/macos-cross
MACOS_CROSS_TESTS := cardio_fd_test cardio_kqueue_test cardio_no_posix_test macos/cardio_apple_config_test
MACOS_INTEL_BINS := $(addprefix $(MACOS_CROSS_DIR)/x86_64/,$(MACOS_CROSS_TESTS))
MACOS_ARM64_BINS := $(addprefix $(MACOS_CROSS_DIR)/arm64/,$(MACOS_CROSS_TESTS))

.PHONY: macos-cross
macos-cross: $(MACOS_INTEL_BINS) $(MACOS_ARM64_BINS)

$(ZIG):
	sh tests/macos/install-zig.sh "$(ZIG_DIR)" "$(ZIG_VERSION)" \
		02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239

$(MACOS_INTEL_BINS): ZIG_TARGET := x86_64-macos.10.13
$(MACOS_ARM64_BINS): ZIG_TARGET := aarch64-macos.11.0
$(filter %/cardio_no_posix_test,$(MACOS_INTEL_BINS) $(MACOS_ARM64_BINS)): CXXFLAGS += -DCARDIO_HAS_POSIX_FD=0
MACOS_KQUEUE_BINS := $(filter %/cardio_kqueue_test,$(MACOS_INTEL_BINS) $(MACOS_ARM64_BINS))
$(MACOS_KQUEUE_BINS): CXXFLAGS += -DCARDIO_TEST_KQUEUE
$(MACOS_KQUEUE_BINS): tests/cardio_fd_test.cpp
$(filter-out $(MACOS_KQUEUE_BINS),$(MACOS_INTEL_BINS)): $(MACOS_CROSS_DIR)/x86_64/%: tests/%.cpp
$(filter-out $(MACOS_KQUEUE_BINS),$(MACOS_ARM64_BINS)): $(MACOS_CROSS_DIR)/arm64/%: tests/%.cpp
$(MACOS_INTEL_BINS) $(MACOS_ARM64_BINS): include/cardio.h tests/test_helpers.h tests/macos/Makefile | $(ZIG)
	@mkdir -p "$(@D)"
	$(ZIG) c++ -target $(ZIG_TARGET) -O2 $(CXXFLAGS) $(filter %.cpp,$^) -o "$@"

MACOS_NATIVE_TESTS := $(filter-out $(BUILD_DIR)/cardio_win32_% $(BUILD_DIR)/cardio_fd_test,$(TEST_BINS)) \
	$(NO_EXCEPTIONS_TEST_BIN) $(NO_POSIX_TEST_BIN) $(SUPPLEMENTAL_DISABLED_TEST_BIN) \
	$(PRIMITIVES_DISABLED_TEST_BIN) $(BUILD_DIR)/cardio_apple_config_test $(BUILD_DIR)/cardio_kqueue_test

$(BUILD_DIR)/cardio_apple_config_test: tests/macos/cardio_apple_config_test.cpp include/cardio.h tests/test_helpers.h | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) $< -o "$@" $(LDLIBS)

$(BUILD_DIR)/cardio_kqueue_test: tests/cardio_fd_test.cpp include/cardio.h tests/test_helpers.h | $(BUILD_DIR)
	$(CXX) $(CXXOPT) $(CXXFLAGS) -DCARDIO_TEST_KQUEUE $< -o "$@" $(LDLIBS)

.PHONY: test-macos
test-macos: $(MACOS_NATIVE_TESTS) $(BUILD_DIR)/cardio_fd_test
	@set -e; for binary in $(MACOS_NATIVE_TESTS); do "$$binary"; done
	@set -e; output=$$("$(BUILD_DIR)/cardio_fd_test"); printf '%s\n' "$$output"; \
		printf '%s\n' "$$output" | grep -Fxq 'cardio_fd_test: PASS'
