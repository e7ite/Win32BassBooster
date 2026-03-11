// Verifies endpoint format acceptance and PCM decode-to-stereo behavior.

#include "endpoint_audio_format.hpp"

#include <array>
#include <cstdint>

#include <ksmedia.h>

#include "gtest/gtest.h"

namespace endpoint_audio_format {
namespace {

constexpr DWORD kDefaultSampleRateHz = 48000;
constexpr WORD kMonoChannelCount = 1;
constexpr WORD kStereoChannelCount = 2;
constexpr WORD kBitsPerSample16 = 16;
constexpr WORD kBitsPerSample24 = 24;
constexpr WORD kBitsPerSample32 = 32;
constexpr WORD kBitsPerByte = 8;
constexpr uint32_t kTwoFrames = 2;
constexpr size_t kStereoSamplesForTwoFrames = 4;
constexpr float kDecodeTolerance = 1e-5F;
constexpr float kHalfScale = 0.5F;
constexpr float kNegativeHalfScale = -0.5F;

WAVEFORMATEX MakeFormat(WORD format_tag, WORD channels, WORD bits_per_sample) {
  WAVEFORMATEX format = {};
  format.wFormatTag = format_tag;
  format.nChannels = channels;
  format.wBitsPerSample = bits_per_sample;
  format.nBlockAlign =
      static_cast<WORD>(channels * (bits_per_sample / kBitsPerByte));
  format.nAvgBytesPerSec = kDefaultSampleRateHz * format.nBlockAlign;
  format.nSamplesPerSec = kDefaultSampleRateHz;
  return format;
}

TEST(EndpointAudioFormatTest, AcceptsPackedFloat32Stereo) {
  const WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                         kStereoChannelCount, kBitsPerSample32);
  EXPECT_TRUE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, RejectsFloat32Mono) {
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_IEEE_FLOAT, kMonoChannelCount, kBitsPerSample32);
  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, RejectsPcm16Stereo) {
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_PCM, kStereoChannelCount, kBitsPerSample16);
  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, AcceptsExtensibleFloat32Stereo) {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat(WAVE_FORMAT_EXTENSIBLE, kStereoChannelCount,
                             kBitsPerSample32);
  format.Format.cbSize =
      sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  EXPECT_TRUE(SupportsDirectStereoFloatCopy(format.Format));
}

TEST(EndpointAudioFormatTest, DecodeMono16DuplicatesChannel) {
  constexpr size_t kMono16TwoFrameByteCount = 4;
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_PCM, kMonoChannelCount, kBitsPerSample16);
  const std::array<uint8_t, kMono16TwoFrameByteCount> src = {0x00, 0x40, 0x00,
                                                              0xC0};
  const StereoPcmBuffer decoded = DecodeToStereoFloat(src.data(), kTwoFrames,
                                                      format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kNegativeHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kNegativeHalfScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereo16PreservesLeftRightLanes) {
  constexpr size_t kStereo16TwoFrameByteCount = 8;
  constexpr float kQuarterScale = 0.25F;
  constexpr float kNegativeQuarterScale = -0.25F;
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_PCM, kStereoChannelCount, kBitsPerSample16);
  const std::array<uint8_t, kStereo16TwoFrameByteCount> src = {
      0x00, 0x40,  // frame 0 left: +0.5
      0x00, 0xC0,  // frame 0 right: -0.5
      0x00, 0x20,  // frame 1 left: +0.25
      0x00, 0xE0,  // frame 1 right: -0.25
  };
  const StereoPcmBuffer decoded = DecodeToStereoFloat(src.data(), kTwoFrames,
                                                      format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kNegativeHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kQuarterScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kNegativeQuarterScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeMono24PreservesSamplePolarity) {
  constexpr size_t kMono24TwoFrameByteCount = 6;
  constexpr float kStrongPolarityThreshold = 0.99F;
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_PCM, kMonoChannelCount, kBitsPerSample24);
  const std::array<uint8_t, kMono24TwoFrameByteCount> src = {
      0xFF, 0xFF, 0x7F,  // frame 0: max positive 24-bit
      0x00, 0x00, 0x80,  // frame 1: max negative 24-bit
  };
  const StereoPcmBuffer decoded = DecodeToStereoFloat(src.data(), kTwoFrames,
                                                      format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongPolarityThreshold);
  EXPECT_GT(decoded.samples[1], kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[3], -kStrongPolarityThreshold);
}

TEST(EndpointAudioFormatTest, DecodeStereoFloat32ReturnsIdentity) {
  const WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                         kStereoChannelCount, kBitsPerSample32);
  constexpr float kLeft = 0.75F;
  constexpr float kRight = -0.25F;
  // Two stereo frames of float32: [L0, R0, L1, R1].
  const float src[] = {kLeft, kRight, kRight, kLeft};
  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kTwoFrames, format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kLeft, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kRight, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kLeft, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereoInt32PreservesPolarity) {
  const WAVEFORMATEX format =
      MakeFormat(WAVE_FORMAT_PCM, kStereoChannelCount, kBitsPerSample32);
  // Max positive and max negative 32-bit samples.
  constexpr int32_t kMaxPos = 0x7FFFFFFF;
  constexpr int32_t kMaxNeg = static_cast<int32_t>(0x80000000);
  const int32_t src[] = {kMaxPos, kMaxNeg, kMaxNeg, kMaxPos};
  constexpr float kStrongThreshold = 0.99F;
  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kTwoFrames, format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongThreshold);
  EXPECT_LT(decoded.samples[1], -kStrongThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongThreshold);
  EXPECT_GT(decoded.samples[3], kStrongThreshold);
}

TEST(EndpointAudioFormatTest, DecodeZeroFramesReturnsEmptyBuffer) {
  const WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                         kStereoChannelCount, kBitsPerSample32);
  const float src[] = {1.0F, 1.0F};
  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), /*frames=*/0, format);
  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeNullSrcReturnsEmptyBuffer) {
  const WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                         kStereoChannelCount, kBitsPerSample32);
  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(/*src=*/nullptr, kTwoFrames, format);
  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeUnknownFormatReturnsZeroFilledBuffer) {
  constexpr WORD kBitsPerSample12 = 12;
  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = kStereoChannelCount;
  format.wBitsPerSample = kBitsPerSample12;
  format.nBlockAlign = 3;
  format.nSamplesPerSec = kDefaultSampleRateHz;
  const uint8_t src[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src, kTwoFrames, format);
  // Unknown format: buffer is allocated but samples are zero.
  EXPECT_EQ(decoded.frames, kTwoFrames);
  EXPECT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], 0.0F, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsExtensiblePcm16StereoAsDirectCopy) {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat(WAVE_FORMAT_EXTENSIBLE, kStereoChannelCount,
                             kBitsPerSample16);
  format.Format.cbSize =
      sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format.Format));
}

TEST(EndpointAudioFormatTest, DecodesExtensiblePcm16Stereo) {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat(WAVE_FORMAT_EXTENSIBLE, kStereoChannelCount,
                             kBitsPerSample16);
  format.Format.cbSize =
      sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  // 0x4000 = +0.5 in 16-bit; 0xC000 = -0.5 in 16-bit.
  const std::array<uint8_t, 8> src = {
      0x00, 0x40, 0x00, 0xC0,  // frame 0: L=+0.5, R=-0.5
      0x00, 0xC0, 0x00, 0x40,  // frame 1: L=-0.5, R=+0.5
  };
  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kTwoFrames, format.Format);
  ASSERT_EQ(decoded.frames, kTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kNegativeHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kNegativeHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kHalfScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeSingleFrameWorks) {
  const WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                         kStereoChannelCount, kBitsPerSample32);
  constexpr float kLeft = 0.33F;
  constexpr float kRight = -0.77F;
  const float src[] = {kLeft, kRight};
  constexpr uint32_t kOneFrame = 1;
  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kOneFrame, format);
  ASSERT_EQ(decoded.frames, kOneFrame);
  constexpr size_t kStereoSamplesForOneFrame = 2;
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForOneFrame);
  EXPECT_NEAR(decoded.samples[0], kLeft, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsFloat32WithWrongBlockAlign) {
  WAVEFORMATEX format = MakeFormat(WAVE_FORMAT_IEEE_FLOAT,
                                   kStereoChannelCount, kBitsPerSample32);
  constexpr WORD kWrongBlockAlign = 12;
  format.nBlockAlign = kWrongBlockAlign;
  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

}  // namespace
}  // namespace endpoint_audio_format
