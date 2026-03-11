// Converts endpoint audio frames into stereo float samples.

#ifndef WIN32BASSBOOSTER_SRC_ENDPOINT_AUDIO_FORMAT_HPP_
#define WIN32BASSBOOSTER_SRC_ENDPOINT_AUDIO_FORMAT_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <audioclient.h>

#include <cstdint>
#include <vector>

namespace endpoint_audio_format {

struct StereoPcmBuffer {
  std::vector<float> samples;
  uint32_t frames = 0;
};

// Returns true only when endpoint frames are packed float32 stereo, so DSP
// output can be copied directly with no conversion.
[[nodiscard]] bool SupportsDirectStereoFloatCopy(const WAVEFORMATEX& format);

// Returns `StereoPcmBuffer`: `samples` is interleaved stereo float data
// (L,R,...) and `frames` is the decoded frame count. Returns an empty
// buffer when `src` is null or `frames` is 0.
[[nodiscard]] StereoPcmBuffer DecodeToStereoFloat(const BYTE* src,
                                                  uint32_t frames,
                                                  const WAVEFORMATEX& format);

}  // namespace endpoint_audio_format

#endif  // WIN32BASSBOOSTER_SRC_ENDPOINT_AUDIO_FORMAT_HPP_
