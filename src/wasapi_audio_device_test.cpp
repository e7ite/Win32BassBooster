// Verifies `WasapiAudioDevice` behavior without real audio hardware by using
// constructor-injected clients plus mock COM clients.
// This target keeps lower coverage because the concrete adapter still owns the
// real default-endpoint acquisition and WASAPI client setup path. Covering
// more of that code would require live hardware integration tests or a deeper
// injected boundary below `AudioDevice`. We do not take that extra step here
// because live hardware makes the suite machine-dependent, and a deeper
// injected boundary would distort the production design just to raise
// coverage.

#include "wasapi_audio_device.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

// `MOCK_METHOD` plus this repository's `clang-format` config rewrites raw
// pointer parameters like `BYTE**` into harder-to-read forms such as
// `BYTE * *`. Keep the pointer spellings behind aliases so these mock
// signatures stay readable after the pre-commit formatter runs.
using MockByteBufferOut = BYTE**;
using MockUint32Out = UINT32*;
using MockWaveFormatOut = WAVEFORMATEX**;

class MockAudioClient final : public IAudioClient {
 public:
  MOCK_METHOD(HRESULT, QueryInterface, (REFIID riid, void** object),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, AddRef, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, Release, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, Initialize,
              (AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
               REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity,
               const WAVEFORMATEX* format, LPCGUID audio_session_guid),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetStreamLatency, (REFERENCE_TIME * latency),
              (Calltype(STDMETHODCALLTYPE), override));

  MOCK_METHOD(HRESULT, GetBufferSize, (MockUint32Out out_buffer_size),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetCurrentPadding, (MockUint32Out out_padding),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, IsFormatSupported,
              (AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX* format,
               WAVEFORMATEX** closest_match),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetMixFormat, (MockWaveFormatOut device_format),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetDevicePeriod,
              (REFERENCE_TIME * default_device_period,
               REFERENCE_TIME* minimum_device_period),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, Start, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, Stop, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, Reset, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, SetEventHandle, (HANDLE event_handle),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetService, (REFIID riid, void** service),
              (Calltype(STDMETHODCALLTYPE), override));
};

class MockAudioCaptureClient final : public IAudioCaptureClient {
 public:
  MOCK_METHOD(HRESULT, QueryInterface, (REFIID riid, void** object),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, AddRef, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, Release, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetBuffer,
              (MockByteBufferOut out_data, UINT32* out_frames, DWORD* out_flags,
               UINT64* device_position, UINT64* qpc_position),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, ReleaseBuffer, (UINT32 num_frames_read),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetNextPacketSize, (MockUint32Out out_packet_size),
              (Calltype(STDMETHODCALLTYPE), override));
};

class MockAudioRenderClient final : public IAudioRenderClient {
 public:
  MOCK_METHOD(HRESULT, QueryInterface, (REFIID riid, void** object),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, AddRef, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(ULONG, Release, (), (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, GetBuffer,
              (UINT32 num_frames_requested, BYTE** out_data),
              (Calltype(STDMETHODCALLTYPE), override));
  MOCK_METHOD(HRESULT, ReleaseBuffer,
              (UINT32 num_frames_written, DWORD release_flags),
              (Calltype(STDMETHODCALLTYPE), override));
};

// Returns a stereo 32-bit float `WAVEFORMATEX` describing the given sample
// rate. `WasapiAudioDevice` frees its format pointer with `CoTaskMemFree`, so
// the caller must copy the result into `CoTaskMemAlloc`-allocated memory;
// otherwise the device calls `CoTaskMemFree` on a non-COM pointer and the
// process crashes.
WAVEFORMATEX StereoFloatFormat(DWORD sample_rate_hz) {
  constexpr WORD kStereoChannelCount = 2;
  constexpr WORD kBitsPerSampleFloat = 32;
  constexpr WORD kStereoBlockAlign = 8;
  return {.wFormatTag = WAVE_FORMAT_IEEE_FLOAT,
          .nChannels = kStereoChannelCount,
          .nSamplesPerSec = sample_rate_hz,
          .nAvgBytesPerSec = sample_rate_hz * kStereoBlockAlign,
          .nBlockAlign = kStereoBlockAlign,
          .wBitsPerSample = kBitsPerSampleFloat,
          .cbSize = 0};
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
  MockAudioClient capture_client;
  EXPECT_CALL(capture_client, Start()).WillOnce(Return(E_FAIL));
  EXPECT_CALL(capture_client, Stop()).WillOnce(Return(S_OK));

  MockAudioClient render_client;
  EXPECT_CALL(render_client, Start()).Times(0);
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  WasapiAudioDevice device(
      {.capture_client = &capture_client, .render_client = &render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest, StartStreamsRenderStartFailureStopsCapture) {
  MockAudioClient capture_client;
  EXPECT_CALL(capture_client, Start()).WillOnce(Return(S_OK));
  // `Stop()` is called once by `StopStreams()` and once by `Close()`.
  EXPECT_CALL(capture_client, Stop()).Times(2).WillRepeatedly(Return(S_OK));

  MockAudioClient render_client;
  EXPECT_CALL(render_client, Start()).WillOnce(Return(E_FAIL));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  WasapiAudioDevice device(
      {.capture_client = &capture_client, .render_client = &render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest, StartStreamsStartsConfiguredClients) {
  MockAudioClient capture_client;
  EXPECT_CALL(capture_client, Start()).WillOnce(Return(S_OK));
  EXPECT_CALL(capture_client, Stop()).WillOnce(Return(S_OK));

  MockAudioClient render_client;
  EXPECT_CALL(render_client, Start()).WillOnce(Return(S_OK));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  WasapiAudioDevice device(
      {.capture_client = &capture_client, .render_client = &render_client});

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_TRUE(status.ok());
  device.Close();
}

TEST(WasapiAudioDeviceTest, StopStreamsStopsConfiguredClients) {
  MockAudioClient capture_client;
  // `Stop()` is called once by `StopStreams()` and once by `Close()`.
  EXPECT_CALL(capture_client, Stop()).Times(2).WillRepeatedly(Return(S_OK));

  MockAudioClient render_client;
  // `Stop()` is called once by `StopStreams()` and once by `Close()`.
  EXPECT_CALL(render_client, Stop()).Times(2).WillRepeatedly(Return(S_OK));

  WasapiAudioDevice device(
      {.capture_client = &capture_client, .render_client = &render_client});

  device.StopStreams();
  device.Close();
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
  MockAudioCaptureClient capture_service;
  EXPECT_CALL(capture_service, GetNextPacketSize(_)).WillOnce(Return(E_FAIL));

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device(
      {.capture_service = &capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_FAIL);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsEmptyWhenNoFramesPending) {
  MockAudioCaptureClient capture_service;
  EXPECT_CALL(capture_service, GetNextPacketSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(0U), Return(S_OK)));

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device(
      {.capture_service = &capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsGetBufferError) {
  MockAudioCaptureClient capture_service;
  EXPECT_CALL(capture_service, GetNextPacketSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(1U), Return(S_OK)));
  EXPECT_CALL(capture_service, GetBuffer(_, _, _, nullptr, nullptr))
      .WillOnce(Return(E_FAIL));

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device(
      {.capture_service = &capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_FAIL);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
}

TEST(WasapiAudioDeviceTest, ReadNextPacketReturnsSilentPacket) {
  MockAudioCaptureClient capture_service;
  BYTE* capture_bytes = nullptr;
  EXPECT_CALL(capture_service, GetNextPacketSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(1U), Return(S_OK)));
  EXPECT_CALL(capture_service, GetBuffer(_, _, _, nullptr, nullptr))
      .WillOnce(DoAll(SetArgPointee<0>(capture_bytes), SetArgPointee<1>(2U),
                      SetArgPointee<2>(AUDCLNT_BUFFERFLAGS_SILENT),
                      Return(S_OK)));
  EXPECT_CALL(capture_service, ReleaseBuffer(2U)).WillOnce(Return(S_OK));

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device(
      {.capture_service = &capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_EQ(packet.frames, 2U);
  EXPECT_TRUE(packet.silent);
  EXPECT_TRUE(packet.samples.empty());
}

TEST(WasapiAudioDeviceTest, ReadNextPacketDecodesStereoFloatPacket) {
  MockAudioCaptureClient capture_service;
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};
  std::vector<BYTE> packet_bytes(sizeof(samples));
  std::memcpy(packet_bytes.data(), samples.data(), sizeof(samples));
  BYTE* capture_bytes = packet_bytes.data();
  EXPECT_CALL(capture_service, GetNextPacketSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(1U), Return(S_OK)));
  EXPECT_CALL(capture_service, GetBuffer(_, _, _, nullptr, nullptr))
      .WillOnce(DoAll(SetArgPointee<0>(capture_bytes), SetArgPointee<1>(2U),
                      SetArgPointee<2>(0U), Return(S_OK)));
  EXPECT_CALL(capture_service, ReleaseBuffer(2U)).WillOnce(Return(S_OK));

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device(
      {.capture_service = &capture_service, .format = raw_format});

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, S_OK);
  EXPECT_EQ(packet.frames, 2U);
  EXPECT_FALSE(packet.silent);
  EXPECT_EQ(packet.samples, (std::vector<float>{0.25F, -0.25F, 0.5F, -0.5F}));
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketBeforeOpenReturnsPointerError) {
  WasapiAudioDevice device;

  const HRESULT status =
      device.WriteRenderPacket(std::vector<float>{0.25F, -0.25F});

  EXPECT_EQ(status, E_POINTER);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketWithNoFramesReturnsSuccess) {
  MockAudioClient render_client;
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(std::vector<float>{});

  EXPECT_EQ(status, S_OK);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsBufferSizeError) {
  MockAudioClient render_client;
  EXPECT_CALL(render_client, GetBufferSize(_)).WillOnce(Return(E_FAIL));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  EXPECT_EQ(status, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsPaddingError) {
  MockAudioClient render_client;
  constexpr DWORD kBufferSize4Frames = 4U;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(kBufferSize4Frames), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_)).WillOnce(Return(E_FAIL));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  EXPECT_EQ(status, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsSFalseWhenBufferIsFull) {
  MockAudioClient render_client;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(2U), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_))
      .WillOnce(DoAll(SetArgPointee<0>(1U), Return(S_OK)));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  // `S_FALSE` signals success but no data was written because the render
  // buffer had no room for the requested frames.
  EXPECT_EQ(status, S_FALSE);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsGetBufferError) {
  MockAudioClient render_client;
  constexpr DWORD kBufferSize4Frames = 4U;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(kBufferSize4Frames), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_))
      .WillOnce(DoAll(SetArgPointee<0>(0U), Return(S_OK)));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;
  EXPECT_CALL(render_service, GetBuffer(2U, _)).WillOnce(Return(E_FAIL));

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  EXPECT_EQ(status, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketReturnsReleaseBufferError) {
  MockAudioClient render_client;
  constexpr DWORD kBufferSize4Frames = 4U;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(kBufferSize4Frames), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_))
      .WillOnce(DoAll(SetArgPointee<0>(0U), Return(S_OK)));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;
  std::vector<BYTE> render_buffer(4 * sizeof(float));
  BYTE* render_bytes = render_buffer.data();
  EXPECT_CALL(render_service, GetBuffer(2U, _))
      .WillOnce(DoAll(SetArgPointee<1>(render_bytes), Return(S_OK)));
  EXPECT_CALL(render_service, ReleaseBuffer(2U, 0U)).WillOnce(Return(E_FAIL));

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  EXPECT_EQ(status, E_FAIL);
  device.Close();
}

TEST(WasapiAudioDeviceTest,
     WriteRenderPacketReturnsPointerErrorWhenBufferIsNull) {
  MockAudioClient render_client;
  constexpr DWORD kBufferSize4Frames = 4U;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(kBufferSize4Frames), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_))
      .WillOnce(DoAll(SetArgPointee<0>(0U), Return(S_OK)));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;
  // `GetBuffer` returns a null pointer, simulating a buffer that cannot be
  // written to. The device must still release the acquired buffer; the
  // `AUDCLNT_BUFFERFLAGS_SILENT` flag tells the endpoint to fill with silence
  // instead of reading from the (null) data pointer.
  BYTE* render_bytes = nullptr;
  EXPECT_CALL(render_service, GetBuffer(2U, _))
      .WillOnce(DoAll(SetArgPointee<1>(render_bytes), Return(S_OK)));
  EXPECT_CALL(render_service, ReleaseBuffer(2U, AUDCLNT_BUFFERFLAGS_SILENT))
      .WillOnce(Return(S_OK));

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const HRESULT status = device.WriteRenderPacket(
      std::array<float, 4>{0.25F, -0.25F, 0.5F, -0.5F});

  EXPECT_EQ(status, E_POINTER);
  device.Close();
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketCopiesSamplesIntoRenderBuffer) {
  MockAudioClient render_client;
  constexpr DWORD kBufferSize4Frames = 4U;
  EXPECT_CALL(render_client, GetBufferSize(_))
      .WillOnce(DoAll(SetArgPointee<0>(kBufferSize4Frames), Return(S_OK)));
  EXPECT_CALL(render_client, GetCurrentPadding(_))
      .WillOnce(DoAll(SetArgPointee<0>(0U), Return(S_OK)));
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioRenderClient render_service;
  std::vector<BYTE> render_buffer(4 * sizeof(float));
  BYTE* render_bytes = render_buffer.data();
  EXPECT_CALL(render_service, GetBuffer(2U, _))
      .WillOnce(DoAll(SetArgPointee<1>(render_bytes), Return(S_OK)));
  EXPECT_CALL(render_service, ReleaseBuffer(2U, 0U)).WillOnce(Return(S_OK));

  WasapiAudioDevice device(
      {.render_client = &render_client, .render_service = &render_service});

  const std::vector<float> samples = {0.25F, -0.25F, 0.5F, -0.5F};
  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, S_OK);
  EXPECT_EQ(0, std::memcmp(render_buffer.data(), samples.data(),
                           samples.size() * sizeof(float)));
  device.Close();
}

TEST(WasapiAudioDeviceTest, CloseBeforeOpenKeepsDefaultState) {
  WasapiAudioDevice device;

  device.Close();

  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_TRUE(device.endpoint_name().empty());
}

TEST(WasapiAudioDeviceTest, SampleRateReturnsConfiguredFormatRate) {
  constexpr DWORD kAlternateSampleRateHz = 44100;
  const WAVEFORMATEX format = StereoFloatFormat(kAlternateSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device({.format = raw_format});

  EXPECT_DOUBLE_EQ(device.sample_rate(),
                   static_cast<double>(kAlternateSampleRateHz));
}

TEST(WasapiAudioDeviceTest, EndpointNameReturnsConfiguredName) {
  WasapiAudioDevice device({.endpoint_name = L"Configured Endpoint"});

  EXPECT_EQ(device.endpoint_name(), L"Configured Endpoint");
}

TEST(WasapiAudioDeviceTest, CloseClearsLiveStateAndKeepsEndpointName) {
  MockAudioClient capture_client;
  EXPECT_CALL(capture_client, Stop()).WillOnce(Return(S_OK));

  MockAudioClient render_client;
  EXPECT_CALL(render_client, Stop()).WillOnce(Return(S_OK));

  MockAudioCaptureClient capture_service;

  MockAudioRenderClient render_service;

  constexpr DWORD kSampleRateHz = 48000;
  const WAVEFORMATEX format = StereoFloatFormat(kSampleRateHz);
  auto* raw_format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format)));
  ASSERT_NE(raw_format, nullptr);
  *raw_format = format;

  WasapiAudioDevice device({.capture_client = &capture_client,
                            .render_client = &render_client,
                            .capture_service = &capture_service,
                            .render_service = &render_service,
                            .format = raw_format,
                            .endpoint_name = L"Configured Endpoint"});

  device.Close();

  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_EQ(device.endpoint_name(), L"Configured Endpoint");
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
  MockAudioClient capture_client;
  // `Stop()` is called once by `TryRecover()` and once by `Close()`.
  EXPECT_CALL(capture_client, Stop()).Times(2).WillRepeatedly(Return(S_OK));

  MockAudioClient render_client;
  // `Stop()` is called once by `TryRecover()` and once by `Close()`.
  EXPECT_CALL(render_client, Stop()).Times(2).WillRepeatedly(Return(S_OK));

  WasapiAudioDevice device(
      {.capture_client = &capture_client, .render_client = &render_client});

  EXPECT_FALSE(device.TryRecover(AUDCLNT_E_DEVICE_INVALIDATED));
  device.Close();
}

TEST(WasapiAudioDeviceTest, DoubleCloseIsSafe) {
  WasapiAudioDevice device;

  device.Close();
  device.Close();

  EXPECT_TRUE(device.endpoint_name().empty());
}
