// Verifies precondition guards for the WASAPI audio device without audio
// hardware.

#include "wasapi_audio_device.hpp"

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
