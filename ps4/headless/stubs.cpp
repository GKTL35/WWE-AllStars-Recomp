// Stand-ins for system pieces the PS4 libc lacks. (The game's own hook functions that the headless
// build does not compile come from a generated file, see scripts/wwe-ps4-build-wsl.sh.)
#include <errno.h>
#include <pthread.h>

extern "C" {
// Robust mutexes do not exist on the PS4: the SDK asks, gets ENOSYS and uses plain mutexes.
int pthread_mutexattr_setrobust(pthread_mutexattr_t*, int) { return ENOSYS; }
int pthread_mutex_consistent(pthread_mutex_t*) { return 0; }
}
