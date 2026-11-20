// All Stars Recomp for PS4, stage 2: the recompiled game with the SDK's Vulkan Xenos GPU, shown on VideoOut.
// (Audio and input come next.) Files on the console:
//   /data/wwe/gamedata/default.xex (+ disc contents), /data/wwe/updatedata/default.xexp
// Logs: /data/wwe/game.log and /data/wwe/game.stage.log
#include <rex/graphics/vulkan/graphics_system.h>
#include <rex/image_info.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread.h>
#include <rex/system/xthread.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "generated/default/wweallstars_init.h"
#include "ps4_ui.h"

extern "C" unsigned long long ps4kit_flip_count(void);

namespace {

void Say(const char* msg) {
  std::fprintf(stderr, "[allstars] %s\n", msg);
  if (FILE* f = std::fopen("/data/wwe/game.stage.log", "a")) {
    std::fprintf(f, "%s\n", msg);
    std::fclose(f);
  }
}

[[noreturn]] void Fail(const char* msg, int code) {
  Say(msg);
  REXLOG_ERROR("{}", msg);
  rex::FlushLogging();
  std::_Exit(code);
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories("/data/wwe/user", ec);
  fs::create_directories("/data/wwe/cache", ec);
  Say("start");

  rex::InitLogging(rex::BuildLogConfig("/data/wwe/game.log", "info", {}));
  Say("logging up");

  wwe::ps4::Ps4AppContext context;

  auto graphics = std::make_unique<rex::graphics::vulkan::VulkanGraphicsSystem>();
  if (XFAILED(graphics->SetupPresentation(&context))) {
    Fail("graphics presentation setup FAILED", 10);
  }
  Say("graphics presentation ready");

  auto window = rex::ui::Window::Create(context, "AllStars_Recomp", 1920, 1080);
  if (!window || !window->Open()) {
    Fail("window FAILED", 11);
  }
  if (!graphics->presenter()) {
    Fail("no presenter FAILED", 12);
  }
  window->SetPresenter(graphics->presenter());
  Say("window and presenter ready");

  rex::Runtime runtime("/data/wwe/gamedata", "/data/wwe/user", "/data/wwe/updatedata",
                       "/data/wwe/cache", {});
  runtime.set_app_context(&context);
  runtime.set_display_window(window.get());
  rex::RuntimeConfig config;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.graphics = std::move(graphics);
  config.tool_mode = false;
  Say("runtime constructed");

  auto status = runtime.Setup(PPCImageConfig, std::move(config));
  if (XFAILED(status)) {
    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    Fail("Runtime::Setup FAILED", 1);
  }
  Say("runtime setup ok");
  if (PPCImageConfig.register_modules) PPCImageConfig.register_modules(runtime.kernel_state());
  status = runtime.LoadXexImage("game:\\default.xex");
  if (XFAILED(status)) {
    REXLOG_ERROR("Failed to load XEX: {:08X}", status);
    Fail("LoadXexImage FAILED", 2);
  }
  Say("xex loaded");

  // The guest starts from the UI thread's loop, like ReXApp does.
  context.CallInUIThreadDeferred([&] {
    auto main_thread = runtime.PrepareModuleLaunch();
    if (!main_thread) {
      Fail("PrepareModuleLaunch FAILED", 3);
    }
    if (auto* gfx = runtime.graphics_system()) {
      const uint32_t title_id = runtime.kernel_state()->title_id();
      if (title_id != 0 && !runtime.cache_root().empty()) {
        gfx->InitializeShaderStorage(runtime.cache_root(), title_id, true);
      }
    }
    Say("launching guest main thread");
    main_thread->Resume();
    std::thread([t = std::move(main_thread), &context]() mutable {
      t->Wait(0, 0, 0, nullptr);
      Say("guest main thread ended");
      rex::FlushLogging();
      context.CallInUIThread([&context] { context.QuitFromUIThread(); });
    }).detach();
  });

  // Heartbeat: shows in the stage log whether frames reach the screen.
  std::thread([] {
    unsigned long long last = 0;
    for (int i = 1;; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
      const unsigned long long now = ps4kit_flip_count();
      char line[96];
      std::snprintf(line, sizeof(line), "t+%ds flips=%llu (+%llu)", i * 10, now, now - last);
      Say(line);
      last = now;
      rex::FlushLogging();
    }
  }).detach();

  const int result = context.RunMainMessageLoop();
  rex::FlushLogging();
  std::_Exit(result);
}
