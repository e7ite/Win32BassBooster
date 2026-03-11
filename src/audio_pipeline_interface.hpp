// Abstract contract for the audio pipeline. Lets tests substitute a lightweight
// stand-in without opening a real audio device.

#ifndef WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_
#define WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>

class AudioPipelineInterface {
 public:
  struct Status {
    Status() = default;

    HRESULT code = S_OK;
    std::wstring error_message;

    [[nodiscard]] static Status Ok() { return {}; }

    [[nodiscard]] static Status Error(HRESULT status_code,
                                      std::wstring_view message) {
      Status status;
      status.code = status_code;
      status.error_message = message;
      return status;
    }

    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(code); }
  };

  virtual ~AudioPipelineInterface() = default;

  // Performs one-time setup if needed, then starts the audio processing
  // thread.
  // On failure, returns the failing code plus a readable error message.
  [[nodiscard]] virtual Status Start() = 0;

  // Stops the audio processing thread. Safe to call if not running.
  virtual void Stop() = 0;

  // Sets the boost level [0, 1]: 0 is flat (unity gain), 1 is maximum bass
  // boost. Maps level to gain and exciter via a square-root curve.
  virtual void SetBoostLevel(double level) = 0;

  [[nodiscard]] virtual double gain_db() const = 0;
  [[nodiscard]] virtual const std::wstring& endpoint_name() const = 0;
};

#endif  // WIN32BASSBOOSTER_SRC_AUDIO_PIPELINE_INTERFACE_HPP_
