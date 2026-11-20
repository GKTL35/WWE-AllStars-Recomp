// EXPERIMENTAL / UNTESTED. See ps4_audio.h and PORTING-PS4.md.

#include "ps4_audio.h"

#include <orbis/AudioOut.h>
#include <orbis/UserService.h>

#include <cstring>

#include <rex/assert.h>
#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>

// The SDL backend defines this cvar (src/audio/sdl/sdl_audio_driver.cpp). A PS4
// build does not compile that backend, so the definition moves here. Keep the
// name, category and text identical so settings files stay valid.
REXCVAR_DEFINE_BOOL(audio_mute, false, "Audio", "Mute audio output");

namespace rex::audio::ps4 {

Ps4AudioDriver::Ps4AudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

Ps4AudioDriver::~Ps4AudioDriver() {
  assert_true(frames_queued_.empty());
  assert_true(frames_unused_.empty());
}

bool Ps4AudioDriver::Initialize() {
  if (sceAudioOutInit() < 0) {
    // Already initialised by the host app is fine on most firmwares; the open
    // below is the real test.
    REXAPU_WARN("sceAudioOutInit() returned an error (continuing)");
  }

  // ORBIS_USER_SERVICE_USER_ID_SYSTEM opens the shared main port. The
  // FLOAT_STEREO format is the one the OpenOrbis headers define for float
  // output; 8-channel formats would let the console do the 5.1 mix, but their
  // enum values are not in this toolchain's header yet (see PORTING-PS4.md).
  port_ = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                          kGrainSamples, kSampleRate, ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO);
  if (port_ < 0) {
    REXAPU_ERROR("sceAudioOutOpen() failed: 0x{:08X}", static_cast<uint32_t>(port_));
    port_ = -1;
    return false;
  }

  running_ = true;
  if (pthread_create(&mixer_thread_, nullptr, &Ps4AudioDriver::MixerThreadEntry, this) != 0) {
    REXAPU_ERROR("pthread_create() for the audio mixer failed");
    running_ = false;
    sceAudioOutClose(port_);
    port_ = -1;
    return false;
  }
  mixer_started_ = true;
  REXAPU_INFO("PS4 audio: {} Hz, stereo float, {} sample grain", kSampleRate, kGrainSamples);
  return true;
}

void Ps4AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  float* output_frame;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[kFrameSamples];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }
  std::memcpy(output_frame, input_frame, kFrameSamples * sizeof(float));
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }
}

void Ps4AudioDriver::Shutdown() {
  if (mixer_started_) {
    running_ = false;
    pthread_join(mixer_thread_, nullptr);
    mixer_started_ = false;
  }
  if (port_ >= 0) {
    sceAudioOutClose(port_);
    port_ = -1;
  }
  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    delete[] frames_unused_.top();
    frames_unused_.pop();
  }
  while (!frames_queued_.empty()) {
    delete[] frames_queued_.front();
    frames_queued_.pop();
  }
}

void* Ps4AudioDriver::MixerThreadEntry(void* self) {
  static_cast<Ps4AudioDriver*>(self)->MixerLoop();
  return nullptr;
}

void Ps4AudioDriver::MixerLoop() {
  // sceAudioOutOutput() wants a buffer it can read for a whole grain; keep
  // one per driver and reuse it (the call returns once the data is consumed).
  alignas(64) float out[kGrainSamples * kOutChannels];

  while (running_) {
    float* buffer = nullptr;
    {
      std::unique_lock<std::mutex> guard(frames_mutex_);
      if (!frames_queued_.empty()) {
        buffer = frames_queued_.front();
        frames_queued_.pop();
      }
    }

    if (!buffer) {
      std::memset(out, 0, sizeof(out));
      sceAudioOutOutput(port_, out);  // silence keeps the output clock running
      continue;
    }

    if (REXCVAR_GET(audio_mute)) {
      std::memset(out, 0, sizeof(out));
    } else {
      // Snapshot once per frame, as the SDL callback does.
      const StereoFold fold = GetStereoFold();
      const float gain = GetOutputGain();
      conversion::sequential_6_BE_to_interleaved_2_LE(out, buffer, kGrainSamples, fold, gain);
    }

    {
      std::unique_lock<std::mutex> guard(frames_mutex_);
      frames_unused_.push(buffer);
    }

    sceAudioOutOutput(port_, out);
    const bool released = semaphore_->Release(1, nullptr);
    assert_true(released);
  }
}

// --- Ps4AudioSystem ------------------------------------------------------

std::unique_ptr<AudioSystem> Ps4AudioSystem::Create(
    runtime::FunctionDispatcher* function_dispatcher) {
  return std::make_unique<Ps4AudioSystem>(function_dispatcher);
}

Ps4AudioSystem::Ps4AudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : AudioSystem(function_dispatcher) {}

Ps4AudioSystem::~Ps4AudioSystem() {}

void Ps4AudioSystem::Initialize() {
  AudioSystem::Initialize();
}

X_STATUS Ps4AudioSystem::CreateDriver([[maybe_unused]] size_t index,
                                      rex::thread::Semaphore* semaphore, AudioDriver** out_driver) {
  assert_not_null(out_driver);
  auto driver = new Ps4AudioDriver(memory_, semaphore);
  if (!driver->Initialize()) {
    driver->Shutdown();
    delete driver;
    return X_STATUS_UNSUCCESSFUL;
  }
  *out_driver = driver;
  return X_STATUS_SUCCESS;
}

void Ps4AudioSystem::DestroyDriver(AudioDriver* driver) {
  assert_not_null(driver);
  auto ps4driver = dynamic_cast<Ps4AudioDriver*>(driver);
  assert_not_null(ps4driver);
  ps4driver->Shutdown();
  delete ps4driver;
}

}  // namespace rex::audio::ps4
