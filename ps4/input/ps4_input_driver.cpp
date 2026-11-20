// EXPERIMENTAL / UNTESTED. See ps4_input_driver.h and PORTING-PS4.md.

#include "ps4_input_driver.h"

#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

#include <rex/logging.h>

namespace rex::input::ps4 {

namespace {

// 0x50533400 = "PS4" + slot, the same style as the NOP driver's constant.
constexpr uint64_t kIdBase = 0x50533400;

struct ButtonMap {
  uint32_t ps4;
  uint16_t xinput;
};

// Cross/Circle/Square/Triangle -> A/B/X/Y keeps the physical positions of the
// 360 pad. The guide button has no PS4 counterpart the app can read.
constexpr ButtonMap kButtons[] = {
    {ORBIS_PAD_BUTTON_UP, X_INPUT_GAMEPAD_DPAD_UP},
    {ORBIS_PAD_BUTTON_DOWN, X_INPUT_GAMEPAD_DPAD_DOWN},
    {ORBIS_PAD_BUTTON_LEFT, X_INPUT_GAMEPAD_DPAD_LEFT},
    {ORBIS_PAD_BUTTON_RIGHT, X_INPUT_GAMEPAD_DPAD_RIGHT},
    {ORBIS_PAD_BUTTON_OPTIONS, X_INPUT_GAMEPAD_START},
    {ORBIS_PAD_BUTTON_TOUCH_PAD, X_INPUT_GAMEPAD_BACK},
    {ORBIS_PAD_BUTTON_L3, X_INPUT_GAMEPAD_LEFT_THUMB},
    {ORBIS_PAD_BUTTON_R3, X_INPUT_GAMEPAD_RIGHT_THUMB},
    {ORBIS_PAD_BUTTON_L1, X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {ORBIS_PAD_BUTTON_R1, X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {ORBIS_PAD_BUTTON_CROSS, X_INPUT_GAMEPAD_A},
    {ORBIS_PAD_BUTTON_CIRCLE, X_INPUT_GAMEPAD_B},
    {ORBIS_PAD_BUTTON_SQUARE, X_INPUT_GAMEPAD_X},
    {ORBIS_PAD_BUTTON_TRIANGLE, X_INPUT_GAMEPAD_Y},
};

// scePad sticks are 0..255 with 128 at rest. XInput is -32768..32767.
int16_t StickToAxis(uint8_t value) {
  const int v = (static_cast<int>(value) - 128) * 257;
  return static_cast<int16_t>(std::clamp(v, -32768, 32767));
}

}  // namespace

Ps4InputDriver::Ps4InputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

Ps4InputDriver::~Ps4InputDriver() {
  for (auto& pad : pads_) {
    if (pad.handle >= 0) {
      scePadClose(pad.handle);
      pad.handle = -1;
    }
  }
}

DeviceId Ps4InputDriver::IdForSlot(size_t slot) {
  return static_cast<DeviceId>(kIdBase + slot);
}

Ps4InputDriver::Pad* Ps4InputDriver::PadFor(DeviceId id) {
  const uint64_t raw = static_cast<uint64_t>(id);
  if (raw < kIdBase || raw >= kIdBase + kMaxGuestUsers) {
    return nullptr;
  }
  Pad& pad = pads_[raw - kIdBase];
  return pad.handle >= 0 ? &pad : nullptr;
}

X_STATUS Ps4InputDriver::Setup() {
  // Both calls are idempotent on the firmwares homebrew runs on; an "already
  // initialised" error from either is not a failure.
  sceUserServiceInitialize(nullptr);
  scePadInit();
  std::lock_guard<std::mutex> lock(mutex_);
  RescanLocked();
  REXLOG_INFO("PS4 input: {} pad(s) opened", pad_count_);
  // No pads is not fatal: they are looked for again while the game polls, so a
  // controller that is switched on or assigned later shows up.
  return X_STATUS_SUCCESS;
}

// Opens a pad for every logged-in user that has one and is not open yet.
void Ps4InputDriver::RescanLocked() {
  OrbisUserServiceLoginUserIdList users;
  std::memset(&users, 0, sizeof(users));
  if (sceUserServiceGetLoginUserIdList(&users) < 0) {
    return;
  }
  for (const auto user : users.userId) {
    if (user == ORBIS_USER_SERVICE_USER_ID_INVALID) {
      continue;
    }
    bool open_already = false;
    for (const auto& pad : pads_) {
      if (pad.handle >= 0 && pad.user_id == user) {
        open_already = true;
      }
    }
    if (open_already) {
      continue;
    }
    Pad* slot = nullptr;
    for (auto& pad : pads_) {
      if (pad.handle < 0) {
        slot = &pad;
        break;
      }
    }
    if (!slot) {
      break;
    }
    const int32_t handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    if (handle < 0) {
      continue;  // logged in, no pad assigned (yet)
    }
    slot->user_id = user;
    slot->handle = handle;
    slot->packet = 0;
    slot->last_buttons = 0;
  }
  pad_count_ = 0;
  for (const auto& pad : pads_) {
    pad_count_ += pad.handle >= 0 ? 1 : 0;
  }
}

void Ps4InputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  // A pad that dropped out is closed, and every free slot is looked at again, once a second.
  const auto now = std::chrono::steady_clock::now();
  if (now - last_rescan_ > std::chrono::seconds(1)) {
    last_rescan_ = now;
    for (auto& pad : pads_) {
      if (pad.handle >= 0) {
        OrbisPadData probe;
        std::memset(&probe, 0, sizeof(probe));
        if (scePadReadState(pad.handle, &probe) < 0) {
          scePadClose(pad.handle);
          pad.handle = -1;
        }
      }
    }
    RescanLocked();
  }
  for (size_t i = 0; i < pads_.size(); ++i) {
    if (pads_[i].handle < 0) {
      continue;
    }
    DeviceInfo info;
    info.id = IdForSlot(i);
    info.name = "DualShock 4";
    info.guid = "ps4-pad-user-" + std::to_string(pads_[i].user_id);
    info.synthetic = false;
    out.push_back(info);
  }
}

X_RESULT Ps4InputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  std::lock_guard<std::mutex> lock(mutex_);
  Pad* pad = PadFor(id);
  if (!pad) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  OrbisPadData data;
  std::memset(&data, 0, sizeof(data));
  if (scePadReadState(pad->handle, &data) < 0 || !data.connected) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (out_state) {
    uint16_t buttons = 0;
    for (const auto& m : kButtons) {
      if (data.buttons & m.ps4) {
        buttons |= m.xinput;
      }
    }

    std::memset(out_state, 0, sizeof(*out_state));
    // A new packet number tells the game the state changed (XInput contract).
    if (data.buttons != pad->last_buttons) {
      pad->last_buttons = data.buttons;
      ++pad->packet;
    }
    out_state->packet_number = pad->packet;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = data.analogButtons.l2;
    out_state->gamepad.right_trigger = data.analogButtons.r2;
    out_state->gamepad.thumb_lx = StickToAxis(data.leftStick.x);
    // Up is positive on XInput, 0 on the PS4's Y axis.
    out_state->gamepad.thumb_ly = static_cast<int16_t>(
        std::clamp(-static_cast<int>(StickToAxis(data.leftStick.y)), -32768, 32767));
    out_state->gamepad.thumb_rx = StickToAxis(data.rightStick.x);
    out_state->gamepad.thumb_ry = static_cast<int16_t>(
        std::clamp(-static_cast<int>(StickToAxis(data.rightStick.y)), -32768, 32767));
  }
  return X_ERROR_SUCCESS;
}

X_RESULT Ps4InputDriver::GetDeviceCapabilities(DeviceId id, uint32_t /*flags*/,
                                               X_INPUT_CAPABILITIES* out_caps) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!PadFor(id)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
    out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
    out_caps->flags = 0;
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT Ps4InputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  std::lock_guard<std::mutex> lock(mutex_);
  Pad* pad = PadFor(id);
  if (!pad) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (vibration) {
    // The 360's left motor is the big low-frequency one, like the DS4's large
    // motor. 16-bit speeds scale down to the PS4's 8-bit.
    OrbisPadVibeParam param;
    param.lgMotor = static_cast<uint8_t>(static_cast<uint16_t>(vibration->left_motor_speed) >> 8);
    param.smMotor = static_cast<uint8_t>(static_cast<uint16_t>(vibration->right_motor_speed) >> 8);
    scePadSetVibration(pad->handle, &param);
  }
  return X_ERROR_SUCCESS;
}

X_RESULT Ps4InputDriver::GetDeviceKeystroke(DeviceId id, uint32_t /*flags*/,
                                            X_INPUT_KEYSTROKE* /*out_keystroke*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!PadFor(id)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Menu-navigation keystrokes are synthesised from state by the game on the
  // 360 build; nothing queued here.
  return X_ERROR_EMPTY;
}

}  // namespace rex::input::ps4
