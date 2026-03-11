// Synchronous process-loopback activation for WASAPI capture clients.

#ifndef WIN32BASSBOOSTER_SRC_LOOPBACK_CAPTURE_ACTIVATION_HPP_
#define WIN32BASSBOOSTER_SRC_LOOPBACK_CAPTURE_ACTIVATION_HPP_

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <windows.h>

#include "audio_pipeline_interface.hpp"

// Activates a loopback capture client that excludes the current process's
// rendered audio from the capture stream, preventing the feedback loop that
// occurs when capturing and rendering on the same endpoint. Returns `Ok` on
// success with ownership of the audio client transferred to `capture_client`;
// otherwise returns the failing HRESULT and `capture_client` is null.
[[nodiscard]] AudioPipelineInterface::Status ActivateLoopbackCaptureClient(
    IAudioClient*& capture_client);

#endif  // WIN32BASSBOOSTER_SRC_LOOPBACK_CAPTURE_ACTIVATION_HPP_
