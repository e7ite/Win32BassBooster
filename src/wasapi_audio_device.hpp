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

// Concrete `AudioDevice` that owns the real WASAPI render endpoint, process
// loopback capture client, and stream recovery path.
class WasapiAudioDevice final : public AudioDevice {
 public:
  // Preconfigured WASAPI clients, services, and endpoint metadata that can be
  // injected at construction time instead of discovered through `Open()`.
  struct Dependencies {
    // Shared-mode loopback capture client to own.
    IAudioClient* capture_client = nullptr;
    // Shared-mode render client to own.
    IAudioClient* render_client = nullptr;
    // Packet-reading service acquired from `capture_client`.
    IAudioCaptureClient* capture_service = nullptr;
    // Buffer-writing service acquired from `render_client`.
    IAudioRenderClient* render_service = nullptr;
    // Render mix format that also defines the loopback capture format.
    WAVEFORMATEX* format = nullptr;
    // Friendly name to report from `endpoint_name()`.
    std::wstring endpoint_name;
  };

  // Creates an unopened device that discovers the default render endpoint and
  // stream clients on demand through `Open()`.
  WasapiAudioDevice();
  // Creates a device from `dependencies`, which transfers ownership of the
  // supplied clients, services, format, and endpoint metadata. `format`
  // follows the same `CoTaskMemFree` ownership convention as `Open()`.
  explicit WasapiAudioDevice(Dependencies dependencies);
  ~WasapiAudioDevice() override;

  WasapiAudioDevice(const WasapiAudioDevice&) = delete;
  WasapiAudioDevice& operator=(const WasapiAudioDevice&) = delete;

  // Acquires the current default render endpoint, activates shared render and
  // process-loopback capture clients, and caches the endpoint label.
  [[nodiscard]] AudioPipelineInterface::Status Open() override;
  // Starts the capture and render streams that were prepared by `Open()`.
  [[nodiscard]] AudioPipelineInterface::Status StartStreams() override;
  // Stops any active capture and render streams. Safe when already stopped.
  void StopStreams() override;
  // Releases the live COM clients and services while keeping the last endpoint
  // label available for UI display.
  void Close() override;

  // Reads one capture packet from the loopback stream and decodes it into
  // interleaved stereo float samples.
  [[nodiscard]] CapturePacket ReadNextPacket() override;
  // Writes interleaved stereo float samples from `pcm` into the render
  // client's next available buffer.
  [[nodiscard]] HRESULT WriteRenderPacket(
      std::span<const float> pcm) override;
  // Reopens and restarts the stream pair after the recoverable WASAPI
  // `failure` that stopped the current stream clients.
  [[nodiscard]] bool TryRecover(HRESULT failure) override;

  // Returns the current render mix rate in Hz, or 0 until `Open()` succeeds.
  [[nodiscard]] double sample_rate() const override;
  // Returns the cached friendly name of the last opened endpoint.
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

  // Enumerator retained from the last successful open/recovery cycle.
  ComPtr<IMMDeviceEnumerator> enumerator_;
  // Default render endpoint that owns the mix format and render stream.
  ComPtr<IMMDevice> render_device_;
  // Shared-mode loopback capture client used to pull system-output packets.
  ComPtr<IAudioClient> capture_client_;
  // Shared-mode render client used to play the processed bass delta.
  ComPtr<IAudioClient> render_client_;
  // Packet-reading service acquired from `capture_client_`.
  ComPtr<IAudioCaptureClient> capture_service_;
  // Buffer-writing service acquired from `render_client_`.
  ComPtr<IAudioRenderClient> render_service_;

  // Render mix format reused as the capture format; process loopback captures
  // in whatever format the render endpoint uses.
  std::unique_ptr<WAVEFORMATEX, CoTaskMemFreeDeleter> format_;

  // Cached friendly endpoint name preserved for UI display even after close.
  std::wstring endpoint_name_;
};

#endif  // WIN32BASSBOOSTER_SRC_WASAPI_AUDIO_DEVICE_HPP_
