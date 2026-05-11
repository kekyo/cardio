// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using plugin_run = bool (*)(cardio::dispatcher*, bool*, int*);

template <typename Function, typename Symbol>
static Function load_function(Symbol symbol) {
  static_assert(sizeof(Function) == sizeof(Symbol));
  auto function = Function{};
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

static plugin_run load_plugin(const char* path, void*& module) {
#if defined(_WIN32)
  auto* handle = ::LoadLibraryA(path);
  CHECK(handle != nullptr);
  auto* symbol = ::GetProcAddress(handle, "cardio_shared_plugin_run");
  CHECK(symbol != nullptr);
  module = handle;
  return load_function<plugin_run>(symbol);
#else
  auto* handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    std::fprintf(stderr, "dlopen failed: %s\n", ::dlerror());
  }
  CHECK(handle != nullptr);
  auto* symbol = ::dlsym(handle, "cardio_shared_plugin_run");
  if (symbol == nullptr) {
    std::fprintf(stderr, "dlsym failed: %s\n", ::dlerror());
  }
  CHECK(symbol != nullptr);
  module = handle;
  return load_function<plugin_run>(symbol);
#endif
}

static void unload_plugin(void* module) {
#if defined(_WIN32)
  CHECK(::FreeLibrary(static_cast<HMODULE>(module)) != 0);
#else
  CHECK_EQ(::dlclose(module), 0);
#endif
}

static void plugin_uses_host_dispatcher(const char* plugin_path) {
  test_dispatcher_host dispatcher;
  auto* module = static_cast<void*>(nullptr);
  auto* run = load_plugin(plugin_path, module);
  auto dispatcher_matched = false;
  auto result = 0;

  CHECK(run(&dispatcher, &dispatcher_matched, &result));
  CHECK(dispatcher_matched);
  CHECK_EQ(result, 0);

  dispatcher.park();

  CHECK_EQ(result, 42);
  unload_plugin(module);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  plugin_uses_host_dispatcher(argv[1]);
  return 0;
}
