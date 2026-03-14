// Captures system output audio, applies DSP, and replays it in real time.
// Designed for low-latency playback while controls change during runtime.

#ifndef WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_
#define WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

#include "audio_device.hpp"
#include "audio_pipeline_interface.hpp"
#include "bass_boost_filter.hpp"

// Coordinates device startup, the background capture/DSP/render loop, and the
// user-facing boost controls.
class AudioPipeline final : public AudioPipelineInterface {
 public:
  // Creates a pipeline with a `WasapiAudioDevice` for production use.
  AudioPipeline();
  // Takes ownership of `device`; the device provides all audio I/O.
  explicit AudioPipeline(std::unique_ptr<AudioDevice> device);
  ~AudioPipeline() override;

  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  // Opens the device, caches endpoint metadata, and starts the background
  // audio thread. Returns the first startup failure from the device.
  [[nodiscard]] AudioPipelineInterface::Status Start() override;
  // Stops the background thread, closes the device, and leaves the pipeline in
  // a safe stopped state even if startup only partially completed.
  void Stop() override;

  // Maps UI boost `level` from [0.0, 1.0] onto the filter gain curve.
  void SetBoostLevel(double level) override {
    const double clamped = std::clamp(level, 0.0, 1.0);
    filter_.SetGainDb(BassBoostFilter::kMaxGainDb * std::sqrt(clamped));
  }

  // Returns whether the background audio thread is currently running.
  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
  // Returns the current bass boost gain in dB.
  [[nodiscard]] double gain_db() const override { return filter_.gain_db(); }
  // Returns the cached friendly name of the most recently opened endpoint.
  [[nodiscard]] const std::wstring& endpoint_name() const override {
    return endpoint_name_;
  }

 private:
  // Owned audio I/O boundary used for startup, packet reads, writes, and
  // recovery.
  std::unique_ptr<AudioDevice> device_;

  // Low-shelf EQ that boosts bass frequencies. Applied first in the DSP chain.
  BassBoostFilter filter_;

  // Runs the capture-DSP-render loop off the UI thread so audio processing
  // never blocks the window message pump.
  std::jthread audio_thread_;
  // Lets callers observe pipeline state without querying the thread directly.
  std::atomic<bool> running_ = false;

  // Cached endpoint label to avoid repeated COM/property-store reads.
  std::wstring endpoint_name_;
};

#endif  // WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_
