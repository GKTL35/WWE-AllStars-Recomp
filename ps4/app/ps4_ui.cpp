#include "ps4_ui.h"

#include <chrono>

#include <rex/ui/surface_ps4.h>

namespace wwe::ps4 {

void Ps4AppContext::NotifyUILoopOfPendingFunctions() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    woke_ = true;
  }
  cv_.notify_one();
}

int Ps4AppContext::RunMainMessageLoop() {
  while (!HasQuitFromUIThread()) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(250), [this] { return woke_; });
      woke_ = false;
    }
    ExecutePendingFunctionsFromUIThread();
  }
  return 0;
}

}  // namespace wwe::ps4

namespace rex::ui {

namespace {

class Ps4Window final : public Window {
 public:
  Ps4Window(WindowedAppContext& context, std::string_view title, uint32_t width, uint32_t height)
      : Window(context, title, width, height) {}
  ~Ps4Window() override { EnterDestructor(); }

 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1920, 1080, receiver);
    if (receiver.IsWindowDestroyed()) {
      return true;
    }
    OnFocusUpdate(true, receiver);
    return true;
  }

  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this);
    OnBeforeClose(receiver);
    if (receiver.IsWindowDestroyed()) {
      return;
    }
    OnAfterClose();
  }

  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override {
    if (allowed_types & Surface::kTypeFlag_Ps4VideoOut) {
      return std::make_unique<Ps4VideoOutSurface>();
    }
    return nullptr;
  }

  void RequestPaintImpl() override {
    // Coalesced; callable from any thread. Paints happen on the UI thread.
    if (paint_pending_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    app_context().CallInUIThread([this] {
      paint_pending_.store(false, std::memory_order_release);
      OnPaint();
    });
  }

 private:
  std::atomic<bool> paint_pending_{false};
};

}  // namespace

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context, std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<Ps4Window>(app_context, title, desired_logical_width,
                                     desired_logical_height);
}

}  // namespace rex::ui
