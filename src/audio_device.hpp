// Abstract audio device for capture and render I/O. Lets tests substitute a
// mock device that provides canned audio data without real hardware.

#ifndef WIN32BASSBOOSTER_SRC_AUDIO_DEVICE_HPP_
#define WIN32BASSBOOSTER_SRC_AUDIO_DEVICE_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "audio_pipeline_interface.hpp"

// One read result from the capture stream: decoded interleaved stereo float
// (L, R, L, R, ...), or an error/empty read when `status` or `frames` says so.
struct CapturePacket {
  // `S_OK` on success, or the failing HRESULT from the capture path.
  HRESULT status = S_OK;
  // Interleaved stereo float PCM in L,R order.
  std::vector<float> samples;
  // Number of stereo frames represented in `samples`.
  uint32_t frames = 0;
  // True when the endpoint marked the packet as silence.
  bool silent = false;
};

// Abstract capture and render device boundary used by `AudioPipeline`.
class AudioDevice {
 public:
  virtual ~AudioDevice() = default;

  // Acquires the audio endpoint and sets up capture and render streams.
  // Returns `Ok` on success; otherwise returns the failing HRESULT and message.
  [[nodiscard]] virtual AudioPipelineInterface::Status Open() = 0;

  // Starts both capture and render streams. Requires a prior successful
  // `Open()`.
  [[nodiscard]] virtual AudioPipelineInterface::Status StartStreams() = 0;

  // Stops both capture and render streams. Safe to call when not streaming.
  virtual void StopStreams() = 0;

  // Releases all device resources. Safe to call when already closed.
  virtual void Close() = 0;

  // Reads, decodes, and releases one capture packet as interleaved stereo
  // float PCM. Returns an empty packet (`frames == 0`) when no data is
  // pending.
  [[nodiscard]] virtual CapturePacket ReadNextPacket() = 0;

  // Writes processed stereo float samples to the render buffer. Returns
  // `S_OK` on success, `S_FALSE` when the render buffer cannot accept the
  // frames from `pcm`, or a failing HRESULT on error.
  [[nodiscard]] virtual HRESULT WriteRenderPacket(
      std::span<const float> pcm) = 0;

  // Attempts recovery after a stream failure by re-acquiring the endpoint and
  // restarting streams. `failure` is the HRESULT that broke the current
  // stream pair. Returns true when recovery succeeds; false otherwise.
  [[nodiscard]] virtual bool TryRecover(HRESULT failure) = 0;

  // Returns the device sample rate in Hz (e.g. 48000.0).
  [[nodiscard]] virtual double sample_rate() const = 0;

  // Returns the friendly name of the current audio endpoint.
  [[nodiscard]] virtual const std::wstring& endpoint_name() const = 0;
};

#endif  // WIN32BASSBOOSTER_SRC_AUDIO_DEVICE_HPP_
