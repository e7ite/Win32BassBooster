// Verifies precondition guards without audio hardware, and the full WASAPI
// lifecycle (open, start, read, stop, close) with COM initialized.

#include "wasapi_audio_device.hpp"

#include <objbase.h>

#include <vector>

#include "gtest/gtest.h"

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

TEST(WasapiAudioDeviceTest, ReadNextPacketBeforeOpenReturnsPointerError) {
  WasapiAudioDevice device;

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_EQ(packet.status, E_POINTER);
  EXPECT_TRUE(packet.samples.empty());
  EXPECT_EQ(packet.frames, 0U);
  EXPECT_FALSE(packet.silent);
}

TEST(WasapiAudioDeviceTest, WriteRenderPacketBeforeOpenReturnsPointerError) {
  WasapiAudioDevice device;
  const std::vector<float> samples = {0.25F, -0.25F};

  const HRESULT status = device.WriteRenderPacket(samples);

  EXPECT_EQ(status, E_POINTER);
}

TEST(WasapiAudioDeviceTest, CloseBeforeOpenKeepsDefaultState) {
  WasapiAudioDevice device;

  device.Close();

  EXPECT_DOUBLE_EQ(device.sample_rate(), 0.0);
  EXPECT_TRUE(device.endpoint_name().empty());
}

TEST(WasapiAudioDeviceTest, OpenWithComInitializedSucceeds) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  WasapiAudioDevice device;

  const AudioPipelineInterface::Status status = device.Open();

  ASSERT_TRUE(status.ok());
  EXPECT_GT(device.sample_rate(), 0.0);
  EXPECT_FALSE(device.endpoint_name().empty());

  device.Close();
  CoUninitialize();
}

TEST(WasapiAudioDeviceTest, StartAndStopStreamsAfterOpen) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  WasapiAudioDevice device;
  ASSERT_TRUE(device.Open().ok());

  const AudioPipelineInterface::Status status = device.StartStreams();

  EXPECT_TRUE(status.ok());

  device.StopStreams();
  device.Close();
  CoUninitialize();
}

TEST(WasapiAudioDeviceTest, ReadNextPacketAfterStartReturnsValidStatus) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  WasapiAudioDevice device;
  ASSERT_TRUE(device.Open().ok());
  ASSERT_TRUE(device.StartStreams().ok());

  const CapturePacket packet = device.ReadNextPacket();

  EXPECT_TRUE(SUCCEEDED(packet.status));

  device.StopStreams();
  device.Close();
  CoUninitialize();
}

TEST(WasapiAudioDeviceTest, TryRecoverNonRecoverableFailureReturnsFalse) {
  WasapiAudioDevice device;

  EXPECT_FALSE(device.TryRecover(E_FAIL));
}

TEST(WasapiAudioDeviceTest, DoubleCloseIsSafe) {
  WasapiAudioDevice device;

  device.Close();
  device.Close();

  EXPECT_TRUE(device.endpoint_name().empty());
}
