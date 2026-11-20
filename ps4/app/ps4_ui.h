// PS4 replacement for the SDL window layer: one fixed 1920x1080 "window" on VideoOut.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace wwe::ps4 {

class Ps4AppContext final : public rex::ui::WindowedAppContext {
 public:
  Ps4AppContext() = default;
  ~Ps4AppContext() override { ExecutePendingFunctionsFromUIThread(); }

  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override { NotifyUILoopOfPendingFunctions(); }

  // Runs queued UI-thread functions (window paints, game launch) until the app quits.
  int RunMainMessageLoop();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool woke_ = false;
};

}  // namespace wwe::ps4
