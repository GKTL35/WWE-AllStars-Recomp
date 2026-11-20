// EXPERIMENTAL / UNTESTED: DualShock 4 input driver for the ReXGlue runtime.
//
// Implements rex::input::InputDriver (ReXGlue v0.10.0, include/rex/input/
// input_driver.h) on OpenOrbis' scePad. Written against orbis/Pad.h and
// orbis/UserService.h; it has never been compiled or run. See PORTING-PS4.md.
//
// One pad per logged-in PS4 user, up to kMaxGuestUsers (4), which is what the
// game's local multiplayer needs. The pad-to-XInput mapping follows the
// Xbox 360 layout the game expects: Cross/Circle/Square/Triangle are A/B/X/Y,
// OPTIONS is Start, the touch pad click is Back.

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include <rex/input/input_driver.h>

namespace rex::input::ps4 {

class Ps4InputDriver final : public InputDriver {
 public:
  Ps4InputDriver(rex::ui::Window* window, size_t window_z_order);
  ~Ps4InputDriver() override;

  X_STATUS Setup() override;

  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
  struct Pad {
    int32_t user_id = -1;
    int32_t handle = -1;  // scePad handle, < 0 when closed
    uint32_t packet = 0;
    uint32_t last_buttons = 0;
  };

  // Device ids encode the slot, so a stale id never aliases another pad.
  static DeviceId IdForSlot(size_t slot);
  Pad* PadFor(DeviceId id);
  void RescanLocked();  // mutex_ held

  std::array<Pad, kMaxGuestUsers> pads_{};
  size_t pad_count_ = 0;
  std::chrono::steady_clock::time_point last_rescan_{};
  std::mutex mutex_;
};

}  // namespace rex::input::ps4
