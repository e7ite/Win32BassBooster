// WASAPI loopback capture and render device.

#include "wasapi_audio_device.hpp"

// clang-format off
// initguid.h must precede functiondiscoverykeys_devpkey.h: `DEFINE_PROPERTYKEY`
// and `DEFINE_GUID` emit definitions only when `INITGUID` is set.
#include <initguid.h>
// clang-format on

#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

namespace {

template <typename T>
using ScopedComPtr = std::unique_ptr<T, WasapiAudioDevice::ComRelease>;

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

}  // namespace

WasapiAudioDevice::~WasapiAudioDevice() { Close(); }

AudioPipelineInterface::Status WasapiAudioDevice::Open() {
  EndpointAcquisition endpoint = AcquireEndpoint();
  if (!endpoint.status.ok()) {
    return endpoint.status;
  }
  return AudioPipelineInterface::Status::Error(
      E_NOTIMPL, L"`WasapiAudioDevice::Open` still needs stream client setup");
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
