// Native Linux x86-64 self-test of src/core/fiber_ps4.cpp (compiled with -D__PS4__ -DMAP_ANON=MAP_ANONYMOUS).
#include <rex/thread/fiber.h>
#include <cstdio>
#include <cmath>
using rex::thread::Fiber;
static Fiber *g_main, *g_a, *g_b;
static int g_log[64], g_n;
static void body(void* arg) {
  int id = (int)(intptr_t)arg;
  volatile double x = 1.5 * id;           // callee-saved FP/regs must survive switches
  for (int i = 0; i < 3; i++) {
    g_log[g_n++] = id * 10 + i;
    Fiber::SwitchTo(id == 1 ? g_b : g_a);
    if (x != 1.5 * id) { std::puts("FP/stack corruption"); std::abort(); }
  }
  Fiber::SwitchTo(g_main);
  for (;;) Fiber::SwitchTo(g_main);
}
int main() {
  g_main = Fiber::ConvertCurrentThread();
  g_a = Fiber::Create(64 * 1024, body, (void*)1);
  g_b = Fiber::Create(64 * 1024, body, (void*)2);
  Fiber::SwitchTo(g_a);   // a0 b0 a1 b1 a2 b2, then a switches to main
  for (int i = 0; i < g_n; i++) std::printf("%d ", g_log[i]);
  std::printf("\nexpected: 10 20 11 21 12 22\nSwitchTo main ok, Current()==main: %d\n", Fiber::Current() == g_main);
  g_a->Destroy(); g_b->Destroy(); g_main->Destroy();
  return 0;
}
