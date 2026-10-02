# Override these commands to use a pinned MinGW toolchain.
AMD64_CXX ?= x86_64-w64-mingw32-g++
I686_CXX ?= i686-w64-mingw32-g++
OUTPUT_DIR ?= ../../build/windows-xp
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -I../../include -I..
LDFLAGS ?= -static -static-libgcc -static-libstdc++
WINE ?= wine
OBJDUMP ?= x86_64-w64-mingw32-objdump

.PHONY: all core cancellation iocp native modern shared test test-core test-cancellation test-iocp test-native test-modern test-shared check-imports
all: core cancellation iocp native modern shared
core: $(OUTPUT_DIR)/amd64-core.exe $(OUTPUT_DIR)/i686-core.exe
cancellation: $(OUTPUT_DIR)/amd64-cancellation.exe $(OUTPUT_DIR)/i686-cancellation.exe
iocp: $(OUTPUT_DIR)/amd64-iocp.exe $(OUTPUT_DIR)/i686-iocp.exe
native: $(OUTPUT_DIR)/amd64-native.exe $(OUTPUT_DIR)/i686-native.exe $(OUTPUT_DIR)/amd64-minimal.exe $(OUTPUT_DIR)/i686-minimal.exe
modern: $(OUTPUT_DIR)/amd64-modern-cancellation.exe $(OUTPUT_DIR)/amd64-modern-iocp.exe $(OUTPUT_DIR)/amd64-modern-native.exe $(OUTPUT_DIR)/i686-modern-cancellation.exe $(OUTPUT_DIR)/i686-modern-iocp.exe $(OUTPUT_DIR)/i686-modern-native.exe
shared: $(OUTPUT_DIR)/amd64-shared-lease.exe $(OUTPUT_DIR)/i686-shared-lease.exe

$(OUTPUT_DIR):
	mkdir -p $@

$(OUTPUT_DIR)/amd64-core.exe: core.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DCARDIO_HAS_WIN32_HANDLE=0 -fno-exceptions -fno-rtti $< -o $@ $(LDFLAGS)

$(OUTPUT_DIR)/i686-core.exe: core.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DCARDIO_HAS_WIN32_HANDLE=0 -fno-exceptions -fno-rtti $< -o $@ $(LDFLAGS)

test-core: core
	$(WINE) $(OUTPUT_DIR)/amd64-core.exe
	$(WINE) $(OUTPUT_DIR)/i686-core.exe

$(OUTPUT_DIR)/amd64-cancellation.exe: cancellation.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-cancellation.exe: cancellation.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $< -o $@ $(LDFLAGS) -luser32

test-cancellation: cancellation
	$(WINE) $(OUTPUT_DIR)/amd64-cancellation.exe
	$(WINE) $(OUTPUT_DIR)/amd64-cancellation.exe --legacy
	$(WINE) $(OUTPUT_DIR)/i686-cancellation.exe
	$(WINE) $(OUTPUT_DIR)/i686-cancellation.exe --legacy

$(OUTPUT_DIR)/amd64-iocp.exe: iocp.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-iocp.exe: iocp.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $< -o $@ $(LDFLAGS) -luser32

test-iocp: iocp
	$(WINE) $(OUTPUT_DIR)/amd64-iocp.exe
	$(WINE) $(OUTPUT_DIR)/amd64-iocp.exe --legacy
	$(WINE) $(OUTPUT_DIR)/i686-iocp.exe
	$(WINE) $(OUTPUT_DIR)/i686-iocp.exe --legacy

$(OUTPUT_DIR)/amd64-native.exe: native.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -fno-exceptions -fno-rtti $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-native.exe: native.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -fno-exceptions -fno-rtti $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/amd64-minimal.exe: native.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -fno-exceptions -fno-rtti -DCARDIO_WITH_SUPPLEMENTAL=0 -DCARDIO_WITH_PRIMITIVES=0 $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-minimal.exe: native.cpp ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -fno-exceptions -fno-rtti -DCARDIO_WITH_SUPPLEMENTAL=0 -DCARDIO_WITH_PRIMITIVES=0 $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/amd64-modern-%.exe: %.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 $< -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-modern-%.exe: %.cpp api_probe.h ../../include/cardio.h ../test_helpers.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 $< -o $@ $(LDFLAGS) -luser32

test-native: native
	$(WINE) $(OUTPUT_DIR)/amd64-native.exe
	$(WINE) $(OUTPUT_DIR)/i686-native.exe
	$(WINE) $(OUTPUT_DIR)/amd64-minimal.exe
	$(WINE) $(OUTPUT_DIR)/i686-minimal.exe

test-modern: modern
	$(WINE) $(OUTPUT_DIR)/amd64-modern-cancellation.exe
	$(WINE) $(OUTPUT_DIR)/amd64-modern-iocp.exe
	$(WINE) $(OUTPUT_DIR)/amd64-modern-native.exe
	$(WINE) $(OUTPUT_DIR)/i686-modern-cancellation.exe
	$(WINE) $(OUTPUT_DIR)/i686-modern-iocp.exe
	$(WINE) $(OUTPUT_DIR)/i686-modern-native.exe

check-imports: all
	node check-imports.mjs "$(OBJDUMP)" "$(OUTPUT_DIR)"

$(OUTPUT_DIR)/amd64-libcardio.dll: ../../samples/libcardio/libcardio.cpp ../../include/cardio.h Makefile | $(OUTPUT_DIR)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -shared $< -o $@ -Wl,--out-implib,$@.a $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-libcardio.dll: ../../samples/libcardio/libcardio.cpp ../../include/cardio.h Makefile | $(OUTPUT_DIR)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -shared $< -o $@ -Wl,--out-implib,$@.a $(LDFLAGS) -luser32

$(OUTPUT_DIR)/amd64-lease-plugin.dll: shared-lease.cpp api_probe.h ../test_helpers.h $(OUTPUT_DIR)/amd64-libcardio.dll Makefile
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DCARDIO_SHARED_LIB=1 -DCARDIO_TEST_PLUGIN -shared $< $(OUTPUT_DIR)/amd64-libcardio.dll.a -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-lease-plugin.dll: shared-lease.cpp api_probe.h ../test_helpers.h $(OUTPUT_DIR)/i686-libcardio.dll Makefile
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DCARDIO_SHARED_LIB=1 -DCARDIO_TEST_PLUGIN -shared $< $(OUTPUT_DIR)/i686-libcardio.dll.a -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/amd64-shared-lease.exe: shared-lease.cpp api_probe.h ../test_helpers.h $(OUTPUT_DIR)/amd64-libcardio.dll $(OUTPUT_DIR)/amd64-lease-plugin.dll Makefile
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DCARDIO_SHARED_LIB=1 $< $(OUTPUT_DIR)/amd64-libcardio.dll.a -o $@ $(LDFLAGS) -luser32

$(OUTPUT_DIR)/i686-shared-lease.exe: shared-lease.cpp api_probe.h ../test_helpers.h $(OUTPUT_DIR)/i686-libcardio.dll $(OUTPUT_DIR)/i686-lease-plugin.dll Makefile
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DCARDIO_SHARED_LIB=1 $< $(OUTPUT_DIR)/i686-libcardio.dll.a -o $@ $(LDFLAGS) -luser32

test-shared: shared
	$(WINE) $(OUTPUT_DIR)/amd64-shared-lease.exe amd64-lease-plugin.dll
	$(WINE) $(OUTPUT_DIR)/i686-shared-lease.exe i686-lease-plugin.dll

test: test-core test-cancellation test-iocp test-native test-modern test-shared check-imports
