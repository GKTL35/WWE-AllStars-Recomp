// EXPERIMENTAL / UNTESTED: PS4 audio backend for the ReXGlue runtime.
//
// Mirrors rex::audio::sdl::SDLAudioDriver (ReXGlue v0.10.0, src/audio/sdl/) but
// sends the mixed frames to the PS4's sceAudioOut instead of SDL. Written
// against OpenOrbis' orbis/AudioOut.h; it has never been compiled or run (no
// PS4 toolchain or console was available when it was written). See
// PORTING-PS4.md.
//
// How it fits: the guest's XAudio2 mixer hands the driver one 5.1 frame at a
// time (6 channels x 256 samples, float, big-endian, planar, 48 kHz) through
// SubmitFrame(). A mixer thread folds each frame to stereo (the same
// StereoFold the SDL driver uses), writes it with sceAudioOutOutput() (which
// blocks for one 256-sample grain and so paces the loop), then releases the
// semaphore the guest audio thread waits on. With nothing queued it outputs
// silence and does not release the semaphore, as the SDL callback does.

#pragma once

#include <pthread.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <stack>

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/thread.h>

namespace rex::audio::ps4 {

class Ps4AudioDriver : public AudioDriver {
 public:
  Ps4AudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~Ps4AudioDriver() override;

  bool Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 private:
  static void* MixerThreadEntry(void* self);
  void MixerLoop();

  rex::thread::Semaphore* semaphore_ = nullptr;

  int32_t port_ = -1;  // sceAudioOut handle
  pthread_t mixer_thread_{};
  bool mixer_started_ = false;
  std::atomic<bool> running_{false};

  // Same frame format as the SDL driver: 5.1, 256 samples per channel, 48 kHz.
  static constexpr uint32_t kSampleRate = 48000;
  static constexpr uint32_t kGuestChannels = 6;
  static constexpr uint32_t kGrainSamples = 256;
  static constexpr uint32_t kFrameSamples = kGuestChannels * kGrainSamples;
  static constexpr uint32_t kOutChannels = 2;  // FLOAT_STEREO

  std::queue<float*> frames_queued_;
  std::stack<float*> frames_unused_;
  std::mutex frames_mutex_;
};

class Ps4AudioSystem : public AudioSystem {
 public:
  explicit Ps4AudioSystem(runtime::FunctionDispatcher* function_dispatcher);
  ~Ps4AudioSystem() override;

  static bool IsAvailable() { return true; }
  static std::unique_ptr<AudioSystem> Create(runtime::FunctionDispatcher* function_dispatcher);

  X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  void DestroyDriver(AudioDriver* driver) override;

 protected:
  void Initialize() override;
};

}  // namespace rex::audio::ps4
