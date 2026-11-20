// glslang's OS layer for the PS4 (its Unix one uses thread cancellation, which the PS4 libc lacks).
// Same interface as glslang/OSDependent/osinclude.h: thread-local slots and one global lock.
#include <pthread.h>

#include <cstdint>
#include <mutex>

namespace glslang {

typedef void* OS_TLSIndex;
#define OS_INVALID_TLS_INDEX ((void*)0)

OS_TLSIndex OS_AllocTLSIndex() {
  pthread_key_t key;
  if (pthread_key_create(&key, nullptr) != 0) {
    return OS_INVALID_TLS_INDEX;
  }
  return reinterpret_cast<OS_TLSIndex>(static_cast<uintptr_t>(key) + 1);
}

bool OS_SetTLSValue(OS_TLSIndex index, void* value) {
  if (index == OS_INVALID_TLS_INDEX) {
    return false;
  }
  return pthread_setspecific(static_cast<pthread_key_t>(reinterpret_cast<uintptr_t>(index) - 1),
                             value) == 0;
}

void* OS_GetTLSValue(OS_TLSIndex index) {
  if (index == OS_INVALID_TLS_INDEX) {
    return nullptr;
  }
  return pthread_getspecific(static_cast<pthread_key_t>(reinterpret_cast<uintptr_t>(index) - 1));
}

bool OS_FreeTLSIndex(OS_TLSIndex index) {
  if (index == OS_INVALID_TLS_INDEX) {
    return false;
  }
  return pthread_key_delete(static_cast<pthread_key_t>(reinterpret_cast<uintptr_t>(index) - 1)) == 0;
}

namespace {
std::recursive_mutex& GlobalMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}
}  // namespace

void InitGlobalLock() {
  (void)GlobalMutex();
}
void GetGlobalLock() {
  GlobalMutex().lock();
}
void ReleaseGlobalLock() {
  GlobalMutex().unlock();
}

void OS_CleanupThreadData(void) {}
void OS_DumpMemoryCounters() {}

}  // namespace glslang
