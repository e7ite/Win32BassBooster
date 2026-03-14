// Audio capture and render pipeline.

#include "audio_pipeline.hpp"

#include <avrt.h>

#include <algorithm>
#include <string_view>
#include <vector>

#include "audio_device.hpp"
#include "bass_boost_filter.hpp"
#include "wasapi_audio_device.hpp"

namespace {

// Keeps the MMCSS registration scoped to the audio thread lifetime so the main
// loop does not need to carry a nullable AVRT handle through every exit path.
class MmThreadCharacteristicsRegistration final {
 public:
  // `task_name` keeps the project-facing interface on `std::wstring_view`.
  // Callers must pass a view backed by a null-terminated wide string because
  // AVRT consumes the raw `PCWSTR` directly.
  explicit MmThreadCharacteristicsRegistration(
      const std::wstring_view task_name) {
    DWORD task_index = 0;
    handle_ = AvSetMmThreadCharacteristicsW(task_name.data(), &task_index);
  }

  ~MmThreadCharacteristicsRegistration() {
    if (handle_ != nullptr) {
      AvRevertMmThreadCharacteristics(handle_);
    }
  }

  MmThreadCharacteristicsRegistration(
      const MmThreadCharacteristicsRegistration&) = delete;
  MmThreadCharacteristicsRegistration& operator=(
      const MmThreadCharacteristicsRegistration&) = delete;

 private:
  // Opaque AVRT registration handle; null when MMCSS registration fails.
  HANDLE handle_ = nullptr;
};

// Processes one captured packet: copies the original, applies the bass boost
// filter, computes the delta (filter output - original), and writes only the
// added bass energy to the render device. Returns `S_OK` on success or when
// the packet is silent/empty; otherwise returns the failing HRESULT.
[[nodiscard]] HRESULT ProcessAndRenderDevicePacket(CapturePacket& packet,
                                                   BassBoostFilter& filter,
                                                   AudioDevice& device) {
  if (packet.silent || packet.frames == 0) {
    return S_OK;
  }

  const std::vector<float> original(packet.samples.begin(),
                                    packet.samples.end());
  filter.ProcessStereo(packet.samples);

  for (size_t i = 0; i < packet.samples.size(); ++i) {
    packet.samples[i] =
        std::clamp(packet.samples[i] - original[i], -1.0F, 1.0F);
  }

  return device.WriteRenderPacket(packet.samples);
}

// Drains all pending capture packets through the DSP chain. Returns `S_OK`
// when the queue is empty or stop was requested; otherwise returns the first
// failing HRESULT.
[[nodiscard]] HRESULT DrainDeviceQueue(AudioDevice& device,
                                       std::stop_token stoken,
                                       BassBoostFilter& filter) {
  while (!stoken.stop_requested()) {
    CapturePacket packet = device.ReadNextPacket();
    if (FAILED(packet.status)) {
      return packet.status;
    }
    if (packet.frames == 0) {
      break;
    }

    if (const HRESULT process =
            ProcessAndRenderDevicePacket(packet, filter, device);
        FAILED(process)) {
      return process;
    }
  }
  return S_OK;
}

// Audio thread entry point. Registers for MMCSS priority, starts streams, and
// polls the capture queue until stop is requested or an unrecoverable failure
// occurs.
void RunDeviceAudioThreadLoop(AudioDevice& device, std::stop_token stoken,
                              BassBoostFilter& filter,
                              std::atomic<bool>& running) {
  // 5 ms poll interval: 1/4 of the 20 ms buffer period. Keeps the capture
  // queue drained without burning CPU while staying well below the buffer
  // duration.
  constexpr DWORD kPollIntervalMs = 5;

  const MmThreadCharacteristicsRegistration pro_audio_task(L"Pro Audio");

  const AudioPipelineInterface::Status start = device.StartStreams();
  if (!start.ok()) {
    running.store(false);
    return;
  }

  while (!stoken.stop_requested()) {
    const HRESULT drain = DrainDeviceQueue(device, stoken, filter);
    if (FAILED(drain) && !device.TryRecover(drain)) {
      break;
    }

    if (FAILED(drain)) {
      filter.SetSampleRate(device.sample_rate());
      continue;
    }

    Sleep(kPollIntervalMs);
  }

  device.StopStreams();
  running.store(false);
}

}  // namespace

AudioPipeline::AudioPipeline()
    : AudioPipeline(std::make_unique<WasapiAudioDevice>()) {}

AudioPipeline::AudioPipeline(std::unique_ptr<AudioDevice> device)
    : device_(std::move(device)) {}

AudioPipeline::~AudioPipeline() { Stop(); }

AudioPipelineInterface::Status AudioPipeline::Start() {
  if (running_.load()) {
    return AudioPipelineInterface::Status::Ok();
  }

  if (const AudioPipelineInterface::Status open = device_->Open(); !open.ok()) {
    return open;
  }

  endpoint_name_ = device_->endpoint_name();
  filter_.SetSampleRate(device_->sample_rate());
  running_.store(true);

  audio_thread_ = std::jthread([this](std::stop_token stoken) {
    RunDeviceAudioThreadLoop(*device_, std::move(stoken), filter_, running_);
  });
  return AudioPipelineInterface::Status::Ok();
}

void AudioPipeline::Stop() {
  audio_thread_.request_stop();
  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }
  device_->Close();
  running_.store(false);
}
