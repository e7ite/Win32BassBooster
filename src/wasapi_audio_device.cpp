// WASAPI loopback capture and render device.

#include "wasapi_audio_device.hpp"

WasapiAudioDevice::~WasapiAudioDevice() { Close(); }

AudioPipelineInterface::Status WasapiAudioDevice::Open() {
  return AudioPipelineInterface::Status::Error(E_NOTIMPL,
                                               L"`WasapiAudioDevice::Open` is not yet implemented");
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
