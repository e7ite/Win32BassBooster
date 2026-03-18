// Verifies endpoint format acceptance and PCM decode-to-stereo behavior.

#include "endpoint_audio_format.hpp"

#include <ksmedia.h>

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

namespace endpoint_audio_format {
namespace {

constexpr DWORD kSharedDefaultSampleRateHz = 48000;
constexpr WORD kSharedMonoChannelCount = 1;
constexpr WORD kSharedStereoChannelCount = 2;
constexpr WORD kSharedBitsPerSample16 = 16;
constexpr WORD kSharedBitsPerSample24 = 24;
constexpr WORD kSharedBitsPerSample32 = 32;
constexpr uint32_t kSharedTwoFrames = 2;
constexpr size_t kSharedStereoSamplesForTwoFrames = 4;
constexpr float kSharedDecodeTolerance = 1e-5F;
constexpr float kSharedHalfScale = 0.5F;
constexpr float kSharedNegativeHalfScale = -0.5F;

// Groups the three fields that vary across test formats so they cannot be
// accidentally transposed.
struct FormatSpec {
  WORD tag;
  WORD channels;
  WORD bits_per_sample;
};

WAVEFORMATEX MakeFormat(FormatSpec spec,
                        DWORD sample_rate = kSharedDefaultSampleRateHz) {
  constexpr int kBitsPerByte = 8;
  WAVEFORMATEX format = {};
  format.wFormatTag = spec.tag;
  format.nChannels = spec.channels;
  format.wBitsPerSample = spec.bits_per_sample;
  format.nBlockAlign =
      static_cast<WORD>(spec.channels * (spec.bits_per_sample / kBitsPerByte));
  format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
  format.nSamplesPerSec = sample_rate;
  return format;
}

TEST(EndpointAudioFormatTest, AcceptsPackedFloat32Stereo) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  EXPECT_TRUE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, RejectsFloat32Mono) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 1,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, RejectsPcm16Stereo) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample16 = 16;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample16},
                                         kDefaultSampleRateHz);

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

TEST(EndpointAudioFormatTest, AcceptsExtensibleFloat32Stereo) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat({.tag = WAVE_FORMAT_EXTENSIBLE,
                              .channels = 2,
                              .bits_per_sample = kBitsPerSample32},
                             kDefaultSampleRateHz);
  // `cbSize` must describe the extra bytes beyond the base `WAVEFORMATEX`
  // header for WASAPI to recognize the extensible format correctly.
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

  EXPECT_TRUE(SupportsDirectStereoFloatCopy(format.Format));
}

TEST(EndpointAudioFormatTest, DecodeMono16DuplicatesChannel) {
  constexpr size_t kMono16TwoFrameByteCount = 4;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = kSharedMonoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample16});
  const std::array<uint8_t, kMono16TwoFrameByteCount> src = {0x00, 0x40, 0x00,
                                                             0xC0};

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kSharedTwoFrames, format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kSharedStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kSharedHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kSharedHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kSharedNegativeHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kSharedNegativeHalfScale, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereo16PreservesLeftRightLanes) {
  constexpr size_t kStereo16TwoFrameByteCount = 8;
  constexpr float kQuarterScale = 0.25F;
  constexpr float kNegativeQuarterScale = -0.25F;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample16});
  const std::array<uint8_t, kStereo16TwoFrameByteCount> src = {
      0x00, 0x40,  // frame 0 left: +0.5
      0x00, 0xC0,  // frame 0 right: -0.5
      0x00, 0x20,  // frame 1 left: +0.25
      0x00, 0xE0,  // frame 1 right: -0.25
  };

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kSharedTwoFrames, format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kSharedStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kSharedHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kSharedNegativeHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kQuarterScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kNegativeQuarterScale, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeMono24PreservesSamplePolarity) {
  constexpr size_t kMono24TwoFrameByteCount = 6;
  constexpr float kStrongPolarityThreshold = 0.99F;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = kSharedMonoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample24});
  const std::array<uint8_t, kMono24TwoFrameByteCount> src = {
      0xFF, 0xFF, 0x7F,  // frame 0: max positive 24-bit
      0x00, 0x00, 0x80,  // frame 1: max negative 24-bit
  };

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kSharedTwoFrames, format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kSharedStereoSamplesForTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongPolarityThreshold);
  EXPECT_GT(decoded.samples[1], kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[3], -kStrongPolarityThreshold);
}

TEST(EndpointAudioFormatTest, DecodeStereoFloat32ReturnsIdentity) {
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample32});
  constexpr float kLeft = 0.75F;
  constexpr float kRight = -0.25F;
  // Two stereo frames of float32: [L0, R0, L1, R1].
  const float src[] = {kLeft, kRight, kRight, kLeft};

  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kSharedTwoFrames, format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kSharedStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kLeft, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kRight, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kLeft, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereoInt32PreservesPolarity) {
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample32});
  // Max positive and max negative 32-bit samples.
  constexpr int32_t kMaxPos = 0x7FFFFFFF;
  constexpr int32_t kMaxNeg = static_cast<int32_t>(0x80000000);
  const int32_t src[] = {kMaxPos, kMaxNeg, kMaxNeg, kMaxPos};
  constexpr float kStrongThreshold = 0.99F;

  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kSharedTwoFrames, format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongThreshold);
  EXPECT_LT(decoded.samples[1], -kStrongThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongThreshold);
  EXPECT_GT(decoded.samples[3], kStrongThreshold);
}

TEST(EndpointAudioFormatTest, DecodeZeroFramesReturnsEmptyBuffer) {
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample32});
  const float src[] = {1.0F, 1.0F};

  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), /*frames=*/0, format);

  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeNullSrcReturnsEmptyBuffer) {
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample32});
  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(/*src=*/nullptr, kSharedTwoFrames, format);

  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeUnknownFormatReturnsZeroFilledBuffer) {
  constexpr WORD kBitsPerSample12 = 12;
  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = kSharedStereoChannelCount;
  format.wBitsPerSample = kBitsPerSample12;
  format.nBlockAlign = 3;
  format.nSamplesPerSec = kSharedDefaultSampleRateHz;
  const uint8_t src[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  const StereoPcmBuffer decoded = DecodeToStereoFloat(src, kSharedTwoFrames, format);

  // Unknown format: buffer is allocated but samples are zero.
  EXPECT_EQ(decoded.frames, kSharedTwoFrames);
  EXPECT_EQ(decoded.samples.size(), kSharedStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], 0.0F, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsExtensiblePcm16StereoAsDirectCopy) {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat({.tag = WAVE_FORMAT_EXTENSIBLE,
                              .channels = kSharedStereoChannelCount,
                              .bits_per_sample = kSharedBitsPerSample16});
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format.Format));
}

TEST(EndpointAudioFormatTest, DecodesExtensiblePcm16Stereo) {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat({.tag = WAVE_FORMAT_EXTENSIBLE,
                              .channels = kSharedStereoChannelCount,
                              .bits_per_sample = kSharedBitsPerSample16});
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  // 0x4000 = +0.5 in 16-bit; 0xC000 = -0.5 in 16-bit.
  const std::array<uint8_t, 8> src = {
      0x00, 0x40, 0x00, 0xC0,  // frame 0: L=+0.5, R=-0.5
      0x00, 0xC0, 0x00, 0x40,  // frame 1: L=-0.5, R=+0.5
  };

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kSharedTwoFrames, format.Format);

  ASSERT_EQ(decoded.frames, kSharedTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kSharedHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kSharedNegativeHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kSharedNegativeHalfScale, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kSharedHalfScale, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeSingleFrameWorks) {
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = kSharedStereoChannelCount,
                                          .bits_per_sample = kSharedBitsPerSample32});
  constexpr float kLeft = 0.33F;
  constexpr float kRight = -0.77F;
  const float src[] = {kLeft, kRight};
  constexpr uint32_t kOneFrame = 1;

  const StereoPcmBuffer decoded = DecodeToStereoFloat(
      reinterpret_cast<const BYTE*>(src), kOneFrame, format);

  ASSERT_EQ(decoded.frames, kOneFrame);
  constexpr size_t kStereoSamplesForOneFrame = 2;
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForOneFrame);
  EXPECT_NEAR(decoded.samples[0], kLeft, kSharedDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kSharedDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsFloat32WithWrongBlockAlign) {
  WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                    .channels = kSharedStereoChannelCount,
                                    .bits_per_sample = kSharedBitsPerSample32});
  constexpr WORD kWrongBlockAlign = 12;
  format.nBlockAlign = kWrongBlockAlign;

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

}  // namespace
}  // namespace endpoint_audio_format
