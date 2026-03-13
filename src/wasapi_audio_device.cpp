// WASAPI loopback capture and render device.

#include "wasapi_audio_device.hpp"

// clang-format off
// initguid.h must precede functiondiscoverykeys_devpkey.h: `DEFINE_PROPERTYKEY`
// and `DEFINE_GUID` emit definitions only when `INITGUID` is set.
#include <initguid.h>
// clang-format on

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include "endpoint_audio_format.hpp"
#include "loopback_capture_activation.hpp"

namespace {

template <typename T>
using ScopedComPtr = std::unique_ptr<T, WasapiAudioDevice::ComRelease>;

using ScopedWaveFormat =
    std::unique_ptr<WAVEFORMATEX, WasapiAudioDevice::CoTaskMemFreeDeleter>;

[[nodiscard]] AudioPipelineInterface::Status ValidateRenderMixFormat(
    const WAVEFORMATEX& render_format) {
  if (endpoint_audio_format::SupportsDirectStereoFloatCopy(render_format)) {
    return AudioPipelineInterface::Status::Ok();
  }
  return AudioPipelineInterface::Status::Error(
      AUDCLNT_E_UNSUPPORTED_FORMAT,
      L"Render mix format is not packed float32 stereo; "
      L"conversion path is unavailable");
}

[[nodiscard]] AudioPipelineInterface::Status InitializeRenderClient(
    IAudioClient& audio_client, WAVEFORMATEX* render_format) {
  // 20 ms is the lowest buffer duration that avoids glitches on most Windows
  // hardware while keeping latency perceptually invisible. WASAPI measures
  // time in 100-nanosecond units; 20 ms = 200,000 units.
  constexpr REFERENCE_TIME kRenderBufferDuration20Ms = 200'000;

  if (const HRESULT initialize_render =
          audio_client.Initialize(AUDCLNT_SHAREMODE_SHARED, /*StreamFlags=*/0,
                                  kRenderBufferDuration20Ms,
                                  /*hnsPeriodicity=*/0, render_format,
                                  /*audioSessionGuid=*/nullptr);
      FAILED(initialize_render)) {
    return AudioPipelineInterface::Status::Error(
        initialize_render, L"IAudioClient::Initialize (render) failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

[[nodiscard]] AudioPipelineInterface::Status AcquireRenderClientService(
    IAudioClient& audio_client, IAudioRenderClient*& raw_audio_render_client) {
  if (const HRESULT render_service = audio_client.GetService(
          __uuidof(IAudioRenderClient),
          reinterpret_cast<void**>(&raw_audio_render_client));
      FAILED(render_service)) {
    return AudioPipelineInterface::Status::Error(
        render_service, L"GetService IAudioRenderClient failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

[[nodiscard]] AudioPipelineInterface::Status InitializeCaptureClient(
    IAudioClient& audio_client, WAVEFORMATEX* capture_format) {
  if (const HRESULT initialize_capture = audio_client.Initialize(
          AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
          /*hnsBufferDuration=*/0,
          /*hnsPeriodicity=*/0, capture_format,
          /*audioSessionGuid=*/nullptr);
      FAILED(initialize_capture)) {
    return AudioPipelineInterface::Status::Error(
        initialize_capture, L"IAudioClient::Initialize (loopback) failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

[[nodiscard]] AudioPipelineInterface::Status AcquireCaptureClientService(
    IAudioClient& audio_client,
    IAudioCaptureClient*& raw_audio_capture_client) {
  if (const HRESULT capture_service = audio_client.GetService(
          __uuidof(IAudioCaptureClient),
          reinterpret_cast<void**>(&raw_audio_capture_client));
      FAILED(capture_service)) {
    return AudioPipelineInterface::Status::Error(
        capture_service, L"GetService IAudioCaptureClient failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

[[nodiscard]] AudioPipelineInterface::Status InitializeCaptureStream(
    IAudioClient& audio_client, WAVEFORMATEX* capture_format,
    IAudioCaptureClient*& raw_audio_capture_client) {
  if (const AudioPipelineInterface::Status capture_init =
          InitializeCaptureClient(audio_client, capture_format);
      !capture_init.ok()) {
    return capture_init;
  }
  return AcquireCaptureClientService(audio_client, raw_audio_capture_client);
}

void ReadEndpointName(IMMDevice* render_device, std::wstring& endpoint_name) {
  if (render_device == nullptr) {
    endpoint_name = L"Default Render Device";
    return;
  }

  IPropertyStore* raw_props = nullptr;
  if (FAILED(render_device->OpenPropertyStore(STGM_READ, &raw_props))) {
    endpoint_name = L"Default Render Device";
    return;
  }

  ScopedComPtr<IPropertyStore> props(raw_props);
  PROPVARIANT prop_variant;
  PropVariantInit(&prop_variant);
  if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &prop_variant)) &&
      prop_variant.vt == VT_LPWSTR) {
    endpoint_name = prop_variant.pwszVal;
  }
  PropVariantClear(&prop_variant);
  if (endpoint_name.empty()) {
    endpoint_name = L"Default Render Device";
  }
}

struct EndpointAcquisition {
  AudioPipelineInterface::Status status;
  ScopedComPtr<IMMDeviceEnumerator> enumerator;
  ScopedComPtr<IMMDevice> render_device;
  std::wstring endpoint_name;
};

[[nodiscard]] EndpointAcquisition AcquireEndpoint() {
  EndpointAcquisition endpoint;

  IMMDeviceEnumerator* raw_enum = nullptr;
  if (const HRESULT create_enumerator = CoCreateInstance(
          __uuidof(MMDeviceEnumerator),
          /*pUnkOuter=*/nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
          reinterpret_cast<void**>(&raw_enum));
      FAILED(create_enumerator)) {
    endpoint.status = AudioPipelineInterface::Status::Error(
        create_enumerator, L"CoCreateInstance(MMDeviceEnumerator) failed");
    return endpoint;
  }
  endpoint.enumerator.reset(raw_enum);

  IMMDevice* raw_device = nullptr;
  if (const HRESULT default_endpoint =
          endpoint.enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                       &raw_device);
      FAILED(default_endpoint)) {
    endpoint.status = AudioPipelineInterface::Status::Error(
        default_endpoint, L"GetDefaultAudioEndpoint failed");
    return endpoint;
  }
  endpoint.render_device.reset(raw_device);

  ReadEndpointName(endpoint.render_device.get(), endpoint.endpoint_name);
  return endpoint;
}

struct RenderClientSetup {
  ScopedComPtr<IAudioClient> audio_client;
  ScopedComPtr<IAudioRenderClient> service;
  ScopedWaveFormat format;
};

struct CaptureClientSetup {
  ScopedComPtr<IAudioClient> audio_client;
  ScopedComPtr<IAudioCaptureClient> service;
};

[[nodiscard]] AudioPipelineInterface::Status SetupRenderClient(
    IMMDevice& render_device, RenderClientSetup& setup) {
  IAudioClient* raw_render = nullptr;
  if (const HRESULT activate_render = render_device.Activate(
          __uuidof(IAudioClient), CLSCTX_ALL,
          /*pActivationParams=*/nullptr, reinterpret_cast<void**>(&raw_render));
      FAILED(activate_render)) {
    return AudioPipelineInterface::Status::Error(
        activate_render, L"Activate render IAudioClient failed");
  }
  setup.audio_client.reset(raw_render);

  WAVEFORMATEX* raw_render_format = nullptr;
  if (const HRESULT render_mix_format =
          setup.audio_client->GetMixFormat(&raw_render_format);
      FAILED(render_mix_format)) {
    return AudioPipelineInterface::Status::Error(
        render_mix_format, L"GetMixFormat (render) failed");
  }
  setup.format.reset(raw_render_format);
  if (const AudioPipelineInterface::Status format_check =
          ValidateRenderMixFormat(*setup.format);
      !format_check.ok()) {
    return format_check;
  }

  if (const AudioPipelineInterface::Status render_init =
          InitializeRenderClient(*setup.audio_client, setup.format.get());
      !render_init.ok()) {
    return render_init;
  }

  IAudioRenderClient* raw_audio_render_client = nullptr;
  if (const AudioPipelineInterface::Status render_service =
          AcquireRenderClientService(*setup.audio_client,
                                     raw_audio_render_client);
      !render_service.ok()) {
    return render_service;
  }
  setup.service.reset(raw_audio_render_client);
  return AudioPipelineInterface::Status::Ok();
}

[[nodiscard]] AudioPipelineInterface::Status SetupCaptureClient(
    WAVEFORMATEX* render_format, CaptureClientSetup& setup) {
  IAudioClient* raw_capture = nullptr;
  if (const AudioPipelineInterface::Status activate =
          ActivateLoopbackCaptureClient(raw_capture);
      !activate.ok()) {
    return activate;
  }
  setup.audio_client.reset(raw_capture);

  IAudioCaptureClient* raw_client = nullptr;
  if (const AudioPipelineInterface::Status capture_stream =
          InitializeCaptureStream(*setup.audio_client, render_format,
                                  raw_client);
      !capture_stream.ok()) {
    return capture_stream;
  }
  setup.service.reset(raw_client);
  return AudioPipelineInterface::Status::Ok();
}

struct StreamClientSetup {
  CaptureClientSetup capture;
  RenderClientSetup render;
};

[[nodiscard]] AudioPipelineInterface::Status SetupStreamClients(
    IMMDevice& render_device, StreamClientSetup& clients) {
  if (const AudioPipelineInterface::Status render =
          SetupRenderClient(render_device, clients.render);
      !render.ok()) {
    return render;
  }
  if (const AudioPipelineInterface::Status capture =
          SetupCaptureClient(clients.render.format.get(), clients.capture);
      !capture.ok()) {
    return capture;
  }
  return AudioPipelineInterface::Status::Ok();
}

}  // namespace

WasapiAudioDevice::~WasapiAudioDevice() { Close(); }

AudioPipelineInterface::Status WasapiAudioDevice::Open() {
  EndpointAcquisition endpoint = AcquireEndpoint();
  if (!endpoint.status.ok()) {
    return endpoint.status;
  }
  StreamClientSetup clients;
  if (const AudioPipelineInterface::Status status =
          SetupStreamClients(*endpoint.render_device, clients);
      !status.ok()) {
    return status;
  }
  enumerator_ = std::move(endpoint.enumerator);
  render_device_ = std::move(endpoint.render_device);
  endpoint_name_ = std::move(endpoint.endpoint_name);
  capture_client_ = std::move(clients.capture.audio_client);
  capture_service_ = std::move(clients.capture.service);
  render_client_ = std::move(clients.render.audio_client);
  render_service_ = std::move(clients.render.service);
  format_ = std::move(clients.render.format);
  return AudioPipelineInterface::Status::Ok();
}

AudioPipelineInterface::Status WasapiAudioDevice::StartStreams() {
  return AudioPipelineInterface::Status::Error(
      E_NOTIMPL, L"`WasapiAudioDevice::StartStreams` is not yet implemented");
}

void WasapiAudioDevice::StopStreams() {}

void WasapiAudioDevice::Close() {
  capture_service_ = nullptr;
  render_service_ = nullptr;
  capture_client_ = nullptr;
  render_client_ = nullptr;
  format_ = nullptr;
  render_device_ = nullptr;
  enumerator_ = nullptr;
  endpoint_name_.clear();
}

CapturePacket WasapiAudioDevice::ReadNextPacket() {
  CapturePacket packet;
  packet.status = E_NOTIMPL;
  return packet;
}

HRESULT WasapiAudioDevice::WriteRenderPacket(std::span<const float> /*pcm*/) {
  return E_NOTIMPL;
}

bool WasapiAudioDevice::TryRecover(HRESULT /*failure*/) { return false; }

double WasapiAudioDevice::sample_rate() const { return 0.0; }

const std::wstring& WasapiAudioDevice::endpoint_name() const {
  return endpoint_name_;
}
