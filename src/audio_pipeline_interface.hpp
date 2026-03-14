// Abstract contract for the audio pipeline. Lets tests substitute a
// lightweight stand-in without opening a real audio device.

#ifndef WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_
#define WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>

// Abstract audio-pipeline contract used by the UI and tests.
class AudioPipelineInterface {
 public:
  // Result of starting the pipeline or opening an audio device.
  struct Status {
    Status() = default;

    // Platform status code for the attempted operation.
    HRESULT code = S_OK;
    // Human-readable explanation when `code` reports failure.
    std::wstring error_message;

    // Returns a success status with `S_OK` and no error message.
    [[nodiscard]] static Status Ok() { return {}; }

    // Returns a failure status with the supplied `status_code` and `message`.
    [[nodiscard]] static Status Error(HRESULT status_code,
                                      std::wstring_view message) {
      Status status;
      status.code = status_code;
      status.error_message = message;
      return status;
    }

    // Returns true when `code` represents success.
    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(code); }
  };

  virtual ~AudioPipelineInterface() = default;

  // Performs one-time setup if needed, then starts the audio processing thread.
  // On failure, returns the failing code plus a readable error message.
  [[nodiscard]] virtual Status Start() = 0;

  // Stops the audio processing thread. Safe to call if not running.
  virtual void Stop() = 0;

  // Sets the boost level [0, 1]: 0 is flat (unity gain), 1 is maximum bass
  // boost. Maps level to gain and exciter via a square-root curve.
  virtual void SetBoostLevel(double level) = 0;

  // Returns the current boost gain in dB.
  [[nodiscard]] virtual double gain_db() const = 0;
  // Returns the friendly name of the active render endpoint, or an empty
  // string when no endpoint has been opened yet.
  [[nodiscard]] virtual const std::wstring& endpoint_name() const = 0;
};

#endif  // WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_
