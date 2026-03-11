// Captures system output audio, applies DSP, and replays it in real time.
// Designed for low-latency playback while controls change during runtime.

#ifndef WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_
#define WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

#include "audio_pipeline_interface.hpp"
#include "bass_boost_filter.hpp"
#include "harmonic_exciter.hpp"

class AudioPipeline final : public AudioPipelineInterface {
 public:
  AudioPipeline();
  ~AudioPipeline() override;

  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  [[nodiscard]] AudioPipelineInterface::Status Start() override;
  void Stop() override;

  void SetBoostLevel(double level) override {
    const double clamped = std::clamp(level, 0.0, 1.0);
    filter_.SetGainDb(BassBoostFilter::kMaxGainDb * std::sqrt(clamped));
    exciter_.SetAmount(clamped);
  }

  [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
  [[nodiscard]] double gain_db() const override { return filter_.gain_db(); }
  [[nodiscard]] double exciter_amount() const noexcept {
    return exciter_.amount();
  }
  [[nodiscard]] const std::wstring& endpoint_name() const override {
    return endpoint_name_;
  }

  // Public because anonymous-namespace helpers in `audio_pipeline.cpp` reuse
  // this deleter for temporary COM ownership while `ComPtr` stays private. COM
  // objects are reference-counted. `Release()` decrements the count and frees
  // the object when it reaches zero - the COM equivalent of delete. This custom
  // deleter lets `unique_ptr` manage COM lifetimes without calling delete.
  struct ComRelease {
    template <typename T>
    void operator()(T* ptr) const noexcept {
      if (ptr != nullptr) {
        ptr->Release();
      }
    }
  };

  // Public because anonymous-namespace helpers in `audio_pipeline.cpp` reuse
  // this deleter for temporary mix-format ownership.
  struct CoTaskMemFreeDeleter {
    void operator()(WAVEFORMATEX* format) const noexcept {
      if (format != nullptr) {
        CoTaskMemFree(format);
      }
    }
  };

 private:
  // Two-step init pattern required by COM factories: T* raw = nullptr;
  // factory(..., &raw); ptr.reset(raw).
  template <typename T>
  using ComPtr = std::unique_ptr<T, ComRelease>;

  // Locates the current default render endpoint during startup and recovery.
  ComPtr<IMMDeviceEnumerator> enumerator_;
  // Holds the speakers/headphones endpoint that loopback capture mirrors.
  ComPtr<IMMDevice> render_device_;
  // Owns the WASAPI loopback stream configuration and lifetime.
  ComPtr<IAudioClient> capture_audio_client_;
  // Owns the WASAPI render stream used to accept processed float output.
  ComPtr<IAudioClient> render_audio_client_;
  // Pulls captured endpoint packets from the loopback audio client.
  ComPtr<IAudioCaptureClient> audio_capture_client_;
  // Exposes the render buffer that receives processed stereo float frames.
  ComPtr<IAudioRenderClient> audio_render_client_;

  // Owns the render mix format with the required `CoTaskMemFree` cleanup.
  // Process loopback captures in the render format, so this serves both paths.
  std::unique_ptr<WAVEFORMATEX, CoTaskMemFreeDeleter> render_format_;

  // These DSP stages are applied in sequence on every audio buffer.
  BassBoostFilter filter_;
  HarmonicExciter exciter_;

  // Runs the capture-DSP-render loop off the UI thread so audio processing
  // never blocks the window message pump. `running_` lets callers observe
  // pipeline state without querying the thread directly.
  std::jthread audio_thread_;
  std::atomic<bool> running_ = false;

  // Cached endpoint label to avoid repeated COM/property-store reads.
  std::wstring endpoint_name_;
};

#endif  // WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_HPP_
