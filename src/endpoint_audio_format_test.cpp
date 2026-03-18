// Verifies endpoint format acceptance and PCM decode-to-stereo behavior.

#include "endpoint_audio_format.hpp"

#include <ksmedia.h>

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

namespace endpoint_audio_format {
namespace {

// Groups the three fields that vary across test formats so they cannot be
// accidentally transposed.
struct FormatSpec {
  WORD tag;
  WORD channels;
  WORD bits_per_sample;
};

WAVEFORMATEX MakeFormat(FormatSpec spec, DWORD sample_rate) {
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
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr size_t kMono16TwoFrameByteCount = 4;
  constexpr size_t kStereoSamplesForTwoFrames = 4;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr float kHalfScale = 0.5F;
  constexpr WORD kBitsPerSample16 = 16;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = 1,
                                          .bits_per_sample = kBitsPerSample16},
                                         kDefaultSampleRateHz);

  // 16-bit PCM samples are little-endian signed integers; 0x4000 = 16384
  // which is 16384/32768 = +0.5, and 0xC000 = -16384 which is -0.5.
  const std::array<uint8_t, kMono16TwoFrameByteCount> src = {0x00, 0x40, 0x00,
                                                             0xC0};

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kTwoFrames, format);

  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], -kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], -kHalfScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereo16PreservesLeftRightLanes) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr size_t kStereo16TwoFrameByteCount = 8;
  constexpr size_t kStereoSamplesForTwoFrames = 4;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr float kHalfScale = 0.5F;
  constexpr float kQuarterScale = 0.25F;
  constexpr float kNegativeQuarterScale = -0.25F;
  constexpr WORD kBitsPerSample16 = 16;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample16},
                                         kDefaultSampleRateHz);

  const std::array<uint8_t, kStereo16TwoFrameByteCount> src = {
      0x00, 0x40,  // frame 0 left: +0.5
      0x00, 0xC0,  // frame 0 right: -0.5
      0x00, 0x20,  // frame 1 left: +0.25
      0x00, 0xE0,  // frame 1 right: -0.25
  };

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kTwoFrames, format);

  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], -kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kQuarterScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kNegativeQuarterScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeMono24PreservesSamplePolarity) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr size_t kMono24TwoFrameByteCount = 6;
  constexpr size_t kStereoSamplesForTwoFrames = 4;
  constexpr float kStrongPolarityThreshold = 0.99F;
  constexpr WORD kBitsPerSample24 = 24;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = 1,
                                          .bits_per_sample = kBitsPerSample24},
                                         kDefaultSampleRateHz);

  // 24-bit PCM samples are 3-byte little-endian two's complement values.
  // 0x7FFFFF is the largest positive value; 0x800000 is the most negative.
  const std::array<uint8_t, kMono24TwoFrameByteCount> src = {
      0xFF, 0xFF, 0x7F,  // frame 0: max positive 24-bit
      0x00, 0x00, 0x80,  // frame 1: max negative 24-bit
  };

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(src.data(), kTwoFrames, format);

  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongPolarityThreshold);
  EXPECT_GT(decoded.samples[1], kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongPolarityThreshold);
  EXPECT_LT(decoded.samples[3], -kStrongPolarityThreshold);
}

TEST(EndpointAudioFormatTest, DecodeStereoFloat32ReturnsIdentity) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr size_t kStereoSamplesForTwoFrames = 4;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr float kLeft = 0.75F;
  constexpr float kRight = -0.25F;
  constexpr WORD kBitsPerSample32 = 32;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  // Two stereo frames of float32: [L0, R0, L1, R1].
  const float src[] = {kLeft, kRight, kRight, kLeft};

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(reinterpret_cast<const BYTE*>(src), kTwoFrames,
                          format);

  ASSERT_EQ(decoded.frames, kTwoFrames);
  ASSERT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], kLeft, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], kRight, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kLeft, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeStereoInt32PreservesPolarity) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_PCM,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  // Max positive and max negative 32-bit samples.
  constexpr int32_t kMaxPos = 0x7FFFFFFF;
  constexpr int32_t kMaxNeg = static_cast<int32_t>(0x80000000);
  const int32_t src[] = {kMaxPos, kMaxNeg, kMaxNeg, kMaxPos};
  constexpr float kStrongThreshold = 0.99F;

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(reinterpret_cast<const BYTE*>(src), kTwoFrames,
                          format);

  ASSERT_EQ(decoded.frames, kTwoFrames);
  EXPECT_GT(decoded.samples[0], kStrongThreshold);
  EXPECT_LT(decoded.samples[1], -kStrongThreshold);
  EXPECT_LT(decoded.samples[2], -kStrongThreshold);
  EXPECT_GT(decoded.samples[3], kStrongThreshold);
}

TEST(EndpointAudioFormatTest, DecodeZeroFramesReturnsEmptyBuffer) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  const float src[] = {1.0F, 1.0F};

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(reinterpret_cast<const BYTE*>(src), 0, format);

  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeNullSrcReturnsEmptyBuffer) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  constexpr uint32_t kTwoFrames = 2;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(/*src=*/nullptr, kTwoFrames, format);

  EXPECT_EQ(decoded.frames, 0U);
  EXPECT_TRUE(decoded.samples.empty());
}

TEST(EndpointAudioFormatTest, DecodeUnknownFormatReturnsZeroFilledBuffer) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample12 = 12;
  constexpr size_t kStereoSamplesForTwoFrames = 4;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr uint32_t kTwoFrames = 2;
  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.wBitsPerSample = kBitsPerSample12;
  format.nBlockAlign = 3;
  format.nSamplesPerSec = kDefaultSampleRateHz;

  const uint8_t src[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  const StereoPcmBuffer decoded = DecodeToStereoFloat(src, kTwoFrames, format);

  // Unknown format: buffer is allocated but samples are zero.
  EXPECT_EQ(decoded.frames, kTwoFrames);
  EXPECT_EQ(decoded.samples.size(), kStereoSamplesForTwoFrames);
  EXPECT_NEAR(decoded.samples[0], 0.0F, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsExtensiblePcm16StereoAsDirectCopy) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample16 = 16;
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat({.tag = WAVE_FORMAT_EXTENSIBLE,
                              .channels = 2,
                              .bits_per_sample = kBitsPerSample16},
                             kDefaultSampleRateHz);
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format.Format));
}

TEST(EndpointAudioFormatTest, DecodesExtensiblePcm16Stereo) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr float kHalfScale = 0.5F;
  constexpr WORD kBitsPerSample16 = 16;
  constexpr uint32_t kTwoFrames = 2;
  WAVEFORMATEXTENSIBLE format = {};
  format.Format = MakeFormat({.tag = WAVE_FORMAT_EXTENSIBLE,
                              .channels = 2,
                              .bits_per_sample = kBitsPerSample16},
                             kDefaultSampleRateHz);
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
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
  EXPECT_NEAR(decoded.samples[1], -kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[2], -kHalfScale, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[3], kHalfScale, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, DecodeSingleFrameWorks) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr float kDecodeTolerance = 1e-5F;
  constexpr float kLeft = 0.33F;
  constexpr float kRight = -0.77F;
  constexpr WORD kBitsPerSample32 = 32;
  const WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                          .channels = 2,
                                          .bits_per_sample = kBitsPerSample32},
                                         kDefaultSampleRateHz);

  const float src[] = {kLeft, kRight};

  const StereoPcmBuffer decoded =
      DecodeToStereoFloat(reinterpret_cast<const BYTE*>(src), 1, format);

  ASSERT_EQ(decoded.frames, 1U);
  ASSERT_EQ(decoded.samples.size(), 2U);
  EXPECT_NEAR(decoded.samples[0], kLeft, kDecodeTolerance);
  EXPECT_NEAR(decoded.samples[1], kRight, kDecodeTolerance);
}

TEST(EndpointAudioFormatTest, RejectsFloat32WithWrongBlockAlign) {
  constexpr DWORD kDefaultSampleRateHz = 48000;
  constexpr WORD kBitsPerSample32 = 32;
  WAVEFORMATEX format = MakeFormat({.tag = WAVE_FORMAT_IEEE_FLOAT,
                                    .channels = 2,
                                    .bits_per_sample = kBitsPerSample32},
                                   kDefaultSampleRateHz);
  constexpr WORD kWrongBlockAlign = 12;
  format.nBlockAlign = kWrongBlockAlign;

  EXPECT_FALSE(SupportsDirectStereoFloatCopy(format));
}

}  // namespace
}  // namespace endpoint_audio_format
