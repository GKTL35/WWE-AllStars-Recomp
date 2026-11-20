// WWE All Stars, PS4 stage 1: boot the recompiled game with no graphics, audio or input and write a log.
// Files expected on the console:
//   /data/wwe/gamedata/default.xex (+ the rest of the disc contents), /data/wwe/updatedata/default.xexp
// Log: /data/wwe/headless.log
#include <rex/image_info.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/runtime.h>
#include <rex/system/thread.h>
#include <rex/system/xthread.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "generated/default/wweallstars_init.h"


static void Say(const char* msg) {
  std::fprintf(stderr, "[wwe-headless] %s\n", msg);
  if (FILE* f = std::fopen("/data/wwe/headless.stage.log", "a")) {
    std::fprintf(f, "%s\n", msg);
    std::fclose(f);
  }
}

int main() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories("/data/wwe/user", ec);
  fs::create_directories("/data/wwe/cache", ec);
  Say("start");

  auto log_config = rex::BuildLogConfig("/data/wwe/headless.log", "info", {});
  rex::InitLogging(log_config);
  Say("logging up");

  rex::Runtime runtime("/data/wwe/gamedata", "/data/wwe/user", "/data/wwe/updatedata", "/data/wwe/cache", {});
  rex::RuntimeConfig config;
  config.kernel_init = rex::kernel::InitializeKernel;  // the Xbox kernel + XAM exports
  config.tool_mode = false;  // no graphics, audio or input system is supplied
  Say("runtime constructed");
  auto status = runtime.Setup(PPCImageConfig, std::move(config));
  if (XFAILED(status)) {
    Say("Runtime::Setup FAILED");
    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    rex::FlushLogging();
    return 1;
  }
  Say("runtime setup ok");
  if (PPCImageConfig.register_modules) PPCImageConfig.register_modules(runtime.kernel_state());
  status = runtime.LoadXexImage("game:\\default.xex");
  if (XFAILED(status)) {
    Say("LoadXexImage FAILED");
    REXLOG_ERROR("Failed to load XEX: {:08X}", status);
    rex::FlushLogging();
    return 2;
  }
  Say("xex loaded");
  auto main_thread = runtime.PrepareModuleLaunch();
  if (!main_thread) {
    Say("PrepareModuleLaunch FAILED");
    rex::FlushLogging();
    return 3;
  }
  Say("launching guest main thread");
  main_thread->Resume();
  // Wait up to 3 minutes, then report and stop: the log shows how far the game got.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    rex::FlushLogging();
  }
  Say("3 minutes passed; stopping");
  rex::FlushLogging();
  std::_Exit(0);
}
