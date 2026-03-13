// WASAPI loopback capture and render device. Manages COM audio clients, format
// negotiation, and stream recovery for the real audio hardware path.

#ifndef WIN32BASSBOOSTER_SRC_WASAPI_AUDIO_DEVICE_HPP_
#define WIN32BASSBOOSTER_SRC_WASAPI_AUDIO_DEVICE_HPP_

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <memory>
#include <string>

#include "audio_device.hpp"

class WasapiAudioDevice final : public AudioDevice {
 public:
  WasapiAudioDevice() = default;
  ~WasapiAudioDevice() override;

  WasapiAudioDevice(const WasapiAudioDevice&) = delete;
  WasapiAudioDevice& operator=(const WasapiAudioDevice&) = delete;

  [[nodiscard]] AudioPipelineInterface::Status Open() override;
  [[nodiscard]] AudioPipelineInterface::Status StartStreams() override;
  void StopStreams() override;
  void Close() override;

  [[nodiscard]] CapturePacket ReadNextPacket() override;
  [[nodiscard]] HRESULT WriteRenderPacket(
      std::span<const float> pcm) override;
  [[nodiscard]] bool TryRecover(HRESULT failure) override;

  [[nodiscard]] double sample_rate() const override;
  [[nodiscard]] const std::wstring& endpoint_name() const override;

  // Custom deleters exposed so anonymous-namespace helpers in the .cpp can
  // reuse them for temporary COM ownership.
  struct ComRelease {
    template <typename T>
    void operator()(T* ptr) const noexcept {
      if (ptr != nullptr) {
        ptr->Release();
      }
    }
  };

  struct CoTaskMemFreeDeleter {
    void operator()(WAVEFORMATEX* format) const noexcept {
      if (format != nullptr) {
        CoTaskMemFree(format);
      }
    }
  };

 private:
  template <typename T>
  using ComPtr = std::unique_ptr<T, ComRelease>;

  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<IMMDevice> render_device_;
  ComPtr<IAudioClient> capture_client_;
  ComPtr<IAudioClient> render_client_;
  ComPtr<IAudioCaptureClient> capture_service_;
  ComPtr<IAudioRenderClient> render_service_;

  // Render mix format reused as the capture format; process loopback captures
  // in whatever format the render endpoint uses.
  std::unique_ptr<WAVEFORMATEX, CoTaskMemFreeDeleter> format_;

  std::wstring endpoint_name_;
};

#endif  // WIN32BASSBOOSTER_SRC_WASAPI_AUDIO_DEVICE_HPP_
