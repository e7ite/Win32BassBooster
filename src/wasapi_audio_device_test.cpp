// Verifies `WasapiAudioDevice` behavior without real audio hardware by using
// constructor-injected clients plus fake COM clients.

#include "wasapi_audio_device.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace {

class FakeComObject {
 public:
  virtual ~FakeComObject() = default;

  HRESULT QueryInterfaceImpl(REFIID /*riid*/, void** object) {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG AddRefImpl() { return ++ref_count_; }

  ULONG ReleaseImpl() {
    const ULONG remaining = --ref_count_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

 private:
  ULONG ref_count_ = 1;
};

class FakeAudioClient final : public IAudioClient, private FakeComObject {
 public:
  void SetStartResult(HRESULT start_result) { start_result_ = start_result; }
  void SetStopResult(HRESULT stop_result) { stop_result_ = stop_result; }
  void SetBufferSizeResult(HRESULT buffer_size_result) {
    buffer_size_result_ = buffer_size_result;
  }
  void SetCurrentPaddingResult(HRESULT current_padding_result) {
    current_padding_result_ = current_padding_result;
  }
  void SetBufferSize(UINT32 buffer_size) { buffer_size_ = buffer_size; }
  void SetCurrentPadding(UINT32 current_padding) {
    current_padding_ = current_padding;
  }

  [[nodiscard]] int start_calls() const { return start_calls_; }
  [[nodiscard]] int stop_calls() const { return stop_calls_; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** object) override {
    return QueryInterfaceImpl(riid, object);
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }

  ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(); }

  HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE /*share_mode*/,
                                       DWORD /*stream_flags*/,
                                       REFERENCE_TIME /*buffer_duration*/,
                                       REFERENCE_TIME /*periodicity*/,
                                       const WAVEFORMATEX* /*format*/,
                                       LPCGUID /*audio_session_guid*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* buffer_size) override {
    if (buffer_size == nullptr) {
      return E_POINTER;
    }
    *buffer_size = buffer_size_;
    return buffer_size_result_;
  }

  HRESULT STDMETHODCALLTYPE GetStreamLatency(
      REFERENCE_TIME* /*latency*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* padding) override {
    if (padding == nullptr) {
      return E_POINTER;
    }
    *padding = current_padding_;
    return current_padding_result_;
  }

  HRESULT STDMETHODCALLTYPE IsFormatSupported(
      AUDCLNT_SHAREMODE /*share_mode*/, const WAVEFORMATEX* /*format*/,
      WAVEFORMATEX** /*closest_match*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** /*device_format*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetDevicePeriod(
      REFERENCE_TIME* /*default_device_period*/,
      REFERENCE_TIME* /*minimum_device_period*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE Start() override {
    ++start_calls_;
    return start_result_;
  }

  HRESULT STDMETHODCALLTYPE Stop() override {
    ++stop_calls_;
    return stop_result_;
  }

  HRESULT STDMETHODCALLTYPE Reset() override { return E_NOTIMPL; }

  HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE /*event_handle*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetService(REFIID /*riid*/,
                                       void** /*service*/) override {
    return E_NOTIMPL;
  }

 private:
  HRESULT start_result_ = S_OK;
  HRESULT stop_result_ = S_OK;
  HRESULT buffer_size_result_ = S_OK;
  HRESULT current_padding_result_ = S_OK;
  UINT32 buffer_size_ = 0;
  UINT32 current_padding_ = 0;
  int start_calls_ = 0;
  int stop_calls_ = 0;
};

class FakeAudioCaptureClient final : public IAudioCaptureClient,
                                     private FakeComObject {
 public:
  void SetGetNextPacketSizeResult(HRESULT get_next_packet_size_result) {
    get_next_packet_size_result_ = get_next_packet_size_result;
  }

  void SetGetBufferResult(HRESULT get_buffer_result) {
    get_buffer_result_ = get_buffer_result;
  }

  void SetNextPacketSize(UINT32 next_packet_size) {
    next_packet_size_ = next_packet_size;
  }

  void SetPacket(std::vector<BYTE> bytes, UINT32 frames, DWORD flags) {
    bytes_ = std::move(bytes);
    frames_ = frames;
    flags_ = flags;
  }

  [[nodiscard]] UINT32 released_frames() const { return released_frames_; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** object) override {
    return QueryInterfaceImpl(riid, object);
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }

  ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(); }

  HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** data, UINT32* frames,
                                      DWORD* flags,
                                      UINT64* /*device_position*/,
                                      UINT64* /*qpc_position*/) override {
    if (data == nullptr || frames == nullptr || flags == nullptr) {
      return E_POINTER;
    }
    if (FAILED(get_buffer_result_)) {
      return get_buffer_result_;
    }
    *data = bytes_.empty() ? nullptr : bytes_.data();
    *frames = frames_;
    *flags = flags_;
    return get_buffer_result_;
  }

  HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 num_frames_read) override {
    released_frames_ = num_frames_read;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32* packet_size) override {
    if (packet_size == nullptr) {
      return E_POINTER;
    }
    *packet_size = next_packet_size_;
    return get_next_packet_size_result_;
  }

 private:
  HRESULT get_next_packet_size_result_ = S_OK;
  HRESULT get_buffer_result_ = S_OK;
  UINT32 next_packet_size_ = 0;
  std::vector<BYTE> bytes_;
  UINT32 frames_ = 0;
  DWORD flags_ = 0;
  UINT32 released_frames_ = 0;
};

class FakeAudioRenderClient final : public IAudioRenderClient,
                                    private FakeComObject {
 public:
  void SetGetBufferResult(HRESULT get_buffer_result) {
    get_buffer_result_ = get_buffer_result;
  }

  void SetReleaseBufferResult(HRESULT release_buffer_result) {
    release_buffer_result_ = release_buffer_result;
  }

  void SetReturnNullBuffer(bool return_null_buffer) {
    return_null_buffer_ = return_null_buffer;
  }

  [[nodiscard]] UINT32 last_requested_frames() const {
    return last_requested_frames_;
  }

  [[nodiscard]] UINT32 last_released_frames() const {
    return last_released_frames_;
  }

  [[nodiscard]] DWORD last_release_flags() const {
    return last_release_flags_;
  }

  [[nodiscard]] std::vector<float> rendered_samples() const {
    std::vector<float> rendered(buffer_.size() / sizeof(float));
    std::memcpy(rendered.data(), buffer_.data(), buffer_.size());
    return rendered;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** object) override {
    return QueryInterfaceImpl(riid, object);
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }

  ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(); }

  HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 num_frames_requested,
                                      BYTE** data) override {
    if (data == nullptr) {
      return E_POINTER;
    }
    last_requested_frames_ = num_frames_requested;
    if (FAILED(get_buffer_result_)) {
      return get_buffer_result_;
    }
    if (return_null_buffer_) {
      *data = nullptr;
      return S_OK;
    }
    buffer_.assign(num_frames_requested * 2 * sizeof(float), 0);
    *data = buffer_.data();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 num_frames_written,
                                          DWORD flags) override {
    last_released_frames_ = num_frames_written;
    last_release_flags_ = flags;
    return release_buffer_result_;
  }

 private:
  HRESULT get_buffer_result_ = S_OK;
  HRESULT release_buffer_result_ = S_OK;
  bool return_null_buffer_ = false;
  UINT32 last_requested_frames_ = 0;
  UINT32 last_released_frames_ = 0;
  DWORD last_release_flags_ = 0;
  std::vector<BYTE> buffer_;
};

WAVEFORMATEX MakeFloatStereoFormat() {
  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = 2;
  format.nSamplesPerSec = 48000;
  format.wBitsPerSample = 32;
  format.nBlockAlign = 8;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  return format;
}

std::vector<BYTE> MakeStereoFloatPacket(const std::array<float, 4>& samples) {
  std::vector<BYTE> bytes(sizeof(samples));
  std::memcpy(bytes.data(), samples.data(), sizeof(samples));
  return bytes;
}

}  // namespace

TEST(WasapiAudioDeviceTest, OpenWithoutComInitializationFails) {
  WasapiAudioDevice device;

  const AudioPipelineInterface::Status status = device.Open();

  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(FAILED(status.code));
  EXPECT_FALSE(status.error_message.empty());
  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_TRUE(device.endpoint_name().empty());
}

TEST(WasapiAudioDeviceTest, StartStreamsBeforeOpenFails) {
  WasapiAudioDevice device;

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_POINTER);
  EXPECT_FALSE(status.error_message.empty());
}

TEST(WasapiAudioDeviceTest, StartStreamsCaptureStartFailureReturnsError) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  capture_client->SetStartResult(E_FAIL);
  WasapiAudioDevice device(
      {.capture_client = capture_client, .render_client = render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  EXPECT_EQ(capture_client->start_calls(), 1);
  EXPECT_EQ(render_client->start_calls(), 0);
}

TEST(WasapiAudioDeviceTest, StartStreamsRenderStartFailureStopsCapture) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  render_client->SetStartResult(E_FAIL);
  WasapiAudioDevice device(
      {.capture_client = capture_client, .render_client = render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  EXPECT_EQ(capture_client->start_calls(), 1);
  EXPECT_EQ(capture_client->stop_calls(), 1);
  EXPECT_EQ(render_client->start_calls(), 1);
}

TEST(WasapiAudioDeviceTest, StartStreamsStartsConfiguredClients) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  WasapiAudioDevice device(
      {.capture_client = capture_client, .render_client = render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(capture_client->start_calls(), 1);
  EXPECT_EQ(render_client->start_calls(), 1);
}

TEST(WasapiAudioDeviceTest, StopStreamsStopsConfiguredClients) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  WasapiAudioDevice device(
      {.capture_client = capture_client, .render_client = render_client});

  device.StopStreams();

  EXPECT_EQ(capture_client->stop_calls(), 1);
  EXPECT_EQ(render_client->stop_calls(), 1);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketBeforeOpenReturnsPointerError) {
  WasapiAudioDevice device;

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_POINTER);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
  EXPECT_FALSE(packet.silent);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsQueryError) {
  auto* capture_service = new FakeAudioCaptureClient();
  capture_service->SetGetNextPacketSizeResult(E_FAIL);
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device(
      {.capture_service = capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_FAIL);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsEmptyWhenNoFramesPending) {
  auto* capture_service = new FakeAudioCaptureClient();
  capture_service->SetNextPacketSize(0);
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device(
      {.capture_service = capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsGetBufferError) {
  auto* capture_service = new FakeAudioCaptureClient();
  capture_service->SetNextPacketSize(1);
  capture_service->SetGetBufferResult(E_FAIL);
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device(
      {.capture_service = capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_FAIL);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsSilentPacket) {
  auto* capture_service = new FakeAudioCaptureClient();
  capture_service->SetNextPacketSize(1);
  capture_service->SetPacket(/*bytes=*/{}, /*frames=*/2,
                             AUDCLNT_BUFFERFLAGS_SILENT);
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device(
      {.capture_service = capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_EQ(packet.frames, 2U);
  EXPECT_TRUE(packet.silent);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(capture_service->released_frames(), 2U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketDecodesStereoFloatPacket) {
  auto* capture_service = new FakeAudioCaptureClient();
  capture_service->SetNextPacketSize(1);
  capture_service->SetPacket(
      MakeStereoFloatPacket({0.25F, -0.25F, 0.5F, -0.5F}), /*frames=*/2,
      /*flags=*/0);
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device(
      {.capture_service = capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_EQ(packet.frames, 2U);
  EXPECT_FALSE(packet.silent);
  EXPECT_EQ(packet.samples,
            (std::vector<float>{0.25F, -0.25F, 0.5F, -0.5F}));
  EXPECT_EQ(capture_service->released_frames(), 2U);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketBeforeOpenReturnsPointerError) {
  WasapiAudioDevice device;
  const std::vector<float> samples = {0.25F, -0.25F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_POINTER);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketWithNoFramesReturnsSuccess) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::vector<float> samples;

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, S_OK);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsBufferSizeError) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSizeResult(E_FAIL);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_FAIL);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsPaddingError) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(4);
  render_client->SetCurrentPaddingResult(E_FAIL);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_FAIL);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsSFalseWhenBufferIsFull) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(2);
  render_client->SetCurrentPadding(1);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, S_FALSE);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsGetBufferError) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(4);
  render_client->SetCurrentPadding(0);
  render_service->SetGetBufferResult(E_FAIL);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_FAIL);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsReleaseBufferError) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(4);
  render_client->SetCurrentPadding(0);
  render_service->SetReleaseBufferResult(E_FAIL);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_FAIL);
  EXPECT_EQ(render_service->last_released_frames(), 2U);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsPointerErrorWhenBufferIsNull) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(4);
  render_client->SetCurrentPadding(0);
  render_service->SetReturnNullBuffer(true);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_POINTER);
  EXPECT_EQ(render_service->last_release_flags(), AUDCLNT_BUFFERFLAGS_SILENT);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketCopiesSamplesIntoRenderBuffer) {
  auto* render_client = new FakeAudioClient();
  auto* render_service = new FakeAudioRenderClient();
  render_client->SetBufferSize(4);
  render_client->SetCurrentPadding(0);
  WasapiAudioDevice device(
      {.render_client = render_client, .render_service = render_service});
  const std::vector<float> samples = {0.25F, -0.25F, 0.5F, -0.5F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, S_OK);
  EXPECT_EQ(render_service->last_requested_frames(), 2U);
  EXPECT_EQ(render_service->last_released_frames(), 2U);
  EXPECT_EQ(render_service->last_release_flags(), 0U);
  EXPECT_EQ(render_service->rendered_samples(), samples);
}

TEST(WasapiAudioDeviceTest, CloseBeforeOpenKeepsDefaultState) {
  WasapiAudioDevice device;

  device.Close();

  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_TRUE(device.endpoint_name().empty());
}

TEST(WasapiAudioDeviceTest, SampleRateReturnsConfiguredFormatRate) {
  WAVEFORMATEX format = MakeFloatStereoFormat();
  format.nSamplesPerSec = 44100;
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device({.format = raw_format});

  EXPECT_DOUBLE_EQ(device.sample_rate(), 44100.0);
}

TEST(WasapiAudioDeviceTest, EndpointNameReturnsConfiguredName) {
  WasapiAudioDevice device({.endpoint_name = L"Configured Endpoint"});

  EXPECT_EQ(device.endpoint_name(), L"Configured Endpoint");
}

TEST(WasapiAudioDeviceTest, CloseClearsLiveStateAndKeepsEndpointName) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  capture_client->AddRef();
  render_client->AddRef();
  const WAVEFORMATEX format = MakeFloatStereoFormat();
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;
  WasapiAudioDevice device({.capture_client = capture_client,
                            .render_client = render_client,
                            .capture_service = new FakeAudioCaptureClient(),
                            .render_service = new FakeAudioRenderClient(),
                            .format = raw_format,
                            .endpoint_name = L"Configured Endpoint"});

  device.Close();

  EXPECT_EQ(capture_client->stop_calls(), 1);
  EXPECT_EQ(render_client->stop_calls(), 1);
  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_EQ(device.endpoint_name(), L"Configured Endpoint");
  capture_client->Release();
  render_client->Release();
}

TEST(WasapiAudioDeviceTest, TryRecoverNonRecoverableFailureReturnsFalse) {
  WasapiAudioDevice device;

  EXPECT_FALSE(device.TryRecover(E_FAIL));
}

TEST(WasapiAudioDeviceTest,
     TryRecoverDeviceInvalidatedWithoutComInitializationReturnsFalse) {
  WasapiAudioDevice device;

  EXPECT_FALSE(device.TryRecover(AUDCLNT_E_DEVICE_INVALIDATED));
}

TEST(WasapiAudioDeviceTest,
     TryRecoverResourcesInvalidatedWithoutComInitializationReturnsFalse) {
  WasapiAudioDevice device;

  EXPECT_FALSE(device.TryRecover(AUDCLNT_E_RESOURCES_INVALIDATED));
}

TEST(WasapiAudioDeviceTest,
     TryRecoverServiceNotRunningWithoutComInitializationReturnsFalse) {
  WasapiAudioDevice device;

  EXPECT_FALSE(device.TryRecover(AUDCLNT_E_SERVICE_NOT_RUNNING));
}

TEST(WasapiAudioDeviceTest,
     TryRecoverRecoverableFailureStopsConfiguredClientsBeforeReturningFalse) {
  auto* capture_client = new FakeAudioClient();
  auto* render_client = new FakeAudioClient();
  WasapiAudioDevice device(
      {.capture_client = capture_client, .render_client = render_client});

  EXPECT_FALSE(device.TryRecover(AUDCLNT_E_DEVICE_INVALIDATED));
  EXPECT_EQ(capture_client->stop_calls(), 1);
  EXPECT_EQ(render_client->stop_calls(), 1);
}

TEST(WasapiAudioDeviceTest, DoubleCloseIsSafe) {
  WasapiAudioDevice device;

  device.Close();
  device.Close();

  EXPECT_TRUE(device.endpoint_name().empty());
}
