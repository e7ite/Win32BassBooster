// Byte-level audio format detection and frame decoding helpers.

#include "endpoint_audio_format.hpp"

#include <ksmedia.h>

#include <cstddef>
#include <cstring>

namespace endpoint_audio_format {
namespace {

// Full-scale divisors for integer-to-float normalization.
constexpr float kInt16FullScale = 32768.0F;       // 2^15
constexpr float kInt32FullScale = 2147483648.0F;  // 2^31

// Bit depths used to distinguish PCM sub-formats in `WAVEFORMATEX`.
constexpr WORD kBitsPerSample16 = 16;
constexpr WORD kBitsPerSample24 = 24;
constexpr WORD kBitsPerSample32 = 32;
constexpr WORD kBitsPerByte = 8;
constexpr WORD kStereoChannelCount = 2;
constexpr WORD kStereoFloat32BlockAlign = 8;

constexpr WORD kExtensibleExtraBytes =
    sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

enum class SampleEncoding {
  kUnknown,
  kFloat32,
  kInt16,
  kInt32,
  kInt24,
};

float DecodeFloat32Sample(const BYTE* sample_bytes) {
  float sample = 0.0F;
  std::memcpy(&sample, sample_bytes, sizeof(sample));
  return sample;
}

float DecodeInt16Sample(const BYTE* sample_bytes) {
  int16_t sample = 0;
  std::memcpy(&sample, sample_bytes, sizeof(sample));
  return static_cast<float>(sample) / kInt16FullScale;
}

float DecodeInt32Sample(const BYTE* sample_bytes) {
  int32_t sample = 0;
  std::memcpy(&sample, sample_bytes, sizeof(sample));
  return static_cast<float>(sample) / kInt32FullScale;
}

float DecodeInt24Sample(const BYTE* sample_bytes) {
  constexpr int kInt24HighByteShift = 24;
  constexpr int kInt24MiddleByteShift = 16;
  constexpr int kInt24LowByteShift = 8;
  // Preserve sign while widening 24-bit PCM to int32; losing it flips polarity
  // for negative samples and introduces distortion.
  const int32_t sample =
      (static_cast<int32_t>(sample_bytes[2]) << kInt24HighByteShift) |
      (static_cast<int32_t>(sample_bytes[1]) << kInt24MiddleByteShift) |
      (static_cast<int32_t>(sample_bytes[0]) << kInt24LowByteShift);
  return static_cast<float>(sample) / kInt32FullScale;
}

using DecodeSampleFunction = float (*)(const BYTE* sample_bytes);

bool TryGetExtensibleSubFormat(const WAVEFORMATEX& format, GUID* sub_format) {
  if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      format.cbSize < kExtensibleExtraBytes) {
    return false;
  }

  const BYTE* format_bytes = reinterpret_cast<const BYTE*>(&format);
  std::memcpy(sub_format,
              format_bytes + offsetof(WAVEFORMATEXTENSIBLE, SubFormat),
              sizeof(*sub_format));
  return true;
}

SampleEncoding DetectEncoding(const WAVEFORMATEX& format) {
  GUID sub_format = {};
  const bool has_extensible_sub_format =
      TryGetExtensibleSubFormat(format, &sub_format);

  const bool is_float32 =
      ((format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
       (has_extensible_sub_format &&
        IsEqualGUID(sub_format, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE)) &&
      format.wBitsPerSample == kBitsPerSample32;
  if (is_float32) {
    return SampleEncoding::kFloat32;
  }

  const bool is_pcm =
      (format.wFormatTag == WAVE_FORMAT_PCM) ||
      (has_extensible_sub_format &&
       IsEqualGUID(sub_format, KSDATAFORMAT_SUBTYPE_PCM) != FALSE);
  if (is_pcm && format.wBitsPerSample == kBitsPerSample16) {
    return SampleEncoding::kInt16;
  }
  if (is_pcm && format.wBitsPerSample == kBitsPerSample32) {
    return SampleEncoding::kInt32;
  }
  if (format.wFormatTag == WAVE_FORMAT_PCM &&
      format.wBitsPerSample == kBitsPerSample24) {
    return SampleEncoding::kInt24;
  }

  return SampleEncoding::kUnknown;
}

}  // namespace

bool SupportsDirectStereoFloatCopy(const WAVEFORMATEX& format) {
  const SampleEncoding sample_encoding = DetectEncoding(format);
  return sample_encoding == SampleEncoding::kFloat32 &&
         format.nChannels == kStereoChannelCount &&
         format.wBitsPerSample == kBitsPerSample32 &&
         format.nBlockAlign == kStereoFloat32BlockAlign;
}

StereoPcmBuffer DecodeToStereoFloat(const BYTE* src, uint32_t frames,
                                    const WAVEFORMATEX& format) {
  StereoPcmBuffer decoded_buffer;
  if (src == nullptr || frames == 0) {
    return decoded_buffer;
  }

  decoded_buffer.frames = frames;
  decoded_buffer.samples.resize(static_cast<size_t>(frames) *
                                kStereoChannelCount);

  if (format.wBitsPerSample % kBitsPerByte != 0 || format.nChannels == 0) {
    return decoded_buffer;
  }
  const SampleEncoding sample_encoding = DetectEncoding(format);
  DecodeSampleFunction decode_sample = nullptr;
  // Dispatch once per buffer; keep format branching out of the sample loop.
  switch (sample_encoding) {
    case SampleEncoding::kFloat32:
      decode_sample = DecodeFloat32Sample;
      break;
    case SampleEncoding::kInt16:
      decode_sample = DecodeInt16Sample;
      break;
    case SampleEncoding::kInt32:
      decode_sample = DecodeInt32Sample;
      break;
    case SampleEncoding::kInt24:
      decode_sample = DecodeInt24Sample;
      break;
    case SampleEncoding::kUnknown:
      return decoded_buffer;
  }

  const uint32_t channels = format.nChannels;
  const uint32_t bytes_per_sample = format.wBitsPerSample / kBitsPerByte;
  const size_t right_channel_offset =
      channels == 1 ? 0 : static_cast<size_t>(bytes_per_sample);

  for (uint32_t frame_index = 0; frame_index < frames; ++frame_index) {
    // Endpoint buffers are interleaved PCM: each frame stores all channel
    // samples contiguously, so stepping by full frame stride avoids crossing
    // channel boundaries.
    const size_t frame_offset =
        static_cast<size_t>(frame_index) * channels * bytes_per_sample;
    const BYTE* frame_start = src + frame_offset;
    const size_t left_index =
        static_cast<size_t>(frame_index) * kStereoChannelCount;
    const size_t right_index = left_index + 1;
    decoded_buffer.samples[left_index] = decode_sample(frame_start);
    decoded_buffer.samples[right_index] =
        decode_sample(frame_start + right_channel_offset);
  }
  return decoded_buffer;
}

}  // namespace endpoint_audio_format
