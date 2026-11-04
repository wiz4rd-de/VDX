#include "rt_hooks.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define VDX_POSIX 1
#endif
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

namespace {
std::atomic<bool> g_on{false};
std::atomic<long> g_allocs{0};
std::atomic<long> g_locks{0};
#if VDX_POSIX
int g_savedOut = -1, g_savedErr = -1;
char g_logPath[256] = {};
#endif
inline void countAlloc() {
  if (g_on.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

// ---- operator new (all platforms) ----
void* operator new(std::size_t n) {
  countAlloc();
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  countAlloc();
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  countAlloc();
  return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  countAlloc();
  return std::malloc(n ? n : 1);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#if defined(__APPLE__)
// libmalloc's stack-logging hook sees every malloc/free in the process.
extern "C" void (*malloc_logger)(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
static void vdxMallocLogger(uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
  if (type & 2u) countAlloc();  // MALLOC_LOG_TYPE_ALLOCATE
}
#endif

#if defined(__linux__) && defined(__GLIBC__)
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void* malloc(size_t n) {
  countAlloc();
  return __libc_malloc(n);
}
extern "C" void* calloc(size_t a, size_t b) {
  countAlloc();
  return __libc_calloc(a, b);
}
extern "C" void* realloc(void* p, size_t n) {
  countAlloc();
  return __libc_realloc(p, n);
}
#endif

#if VDX_POSIX
extern "C" int pthread_mutex_lock(pthread_mutex_t* m) {
  using Fn = int (*)(pthread_mutex_t*);
  static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
  if (g_on.load(std::memory_order_relaxed)) g_locks.fetch_add(1, std::memory_order_relaxed);
  return real(m);
}
#endif

namespace vdxtest::rt {

void begin() {
#if defined(__APPLE__)
  malloc_logger = vdxMallocLogger;
#endif
#if VDX_POSIX
  std::fflush(stdout);
  std::fflush(stderr);
  std::snprintf(g_logPath, sizeof g_logPath, "/tmp/vdx_rt_log_%d.txt", int(getpid()));
  const int fd = open(g_logPath, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  g_savedOut = dup(1);
  g_savedErr = dup(2);
  dup2(fd, 1);
  dup2(fd, 2);
  close(fd);
#endif
  g_allocs = 0;
  g_locks = 0;
  g_on = true;
}

Counters end() {
  g_on = false;
  Counters c;
  c.allocs = g_allocs.load();
  c.locks = g_locks.load();
#if defined(__APPLE__)
  malloc_logger = nullptr;
#endif
#if VDX_POSIX
  std::fflush(stdout);
  std::fflush(stderr);
  dup2(g_savedOut, 1);
  dup2(g_savedErr, 2);
  close(g_savedOut);
  close(g_savedErr);
  struct stat st {};
  if (stat(g_logPath, &st) == 0) c.logBytes = long(st.st_size);
  unlink(g_logPath);
#endif
  return c;
}

const char* coverage() {
#if defined(__linux__) && defined(__GLIBC__)
  return "operator new, malloc/calloc/realloc, pthread_mutex_lock, stdout/stderr";
#elif defined(__APPLE__)
  return "operator new, malloc (malloc_logger), pthread_mutex_lock, stdout/stderr";
#else
  return "operator new";
#endif
}

}  // namespace vdxtest::rt
