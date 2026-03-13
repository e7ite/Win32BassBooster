// Audio capture and render pipeline.

#include "audio_pipeline.hpp"

#include <avrt.h>

#include <algorithm>
#include <vector>

#include "audio_device.hpp"
#include "bass_boost_filter.hpp"
#include "wasapi_audio_device.hpp"

namespace {

// 5 ms poll interval: 1/4 of the 20 ms buffer period. Keeps the capture queue
// drained without burning CPU while staying well below the buffer duration.
constexpr DWORD kPollIntervalMs = 5;

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

[[nodiscard]] HRESULT DrainDeviceQueue(AudioDevice& device,
                                       BassBoostFilter& filter,
                                       std::stop_token stoken) {
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

void RunDeviceAudioThreadLoop(AudioDevice& device, BassBoostFilter& filter,
                              std::atomic<bool>& running,
                              std::stop_token stoken) {
  DWORD task_index = 0;
  HANDLE task =
      AvSetMmThreadCharacteristicsW(/*taskName=*/L"Pro Audio", &task_index);

  const AudioPipelineInterface::Status start = device.StartStreams();
  if (!start.ok()) {
    running.store(false);
    if (task != nullptr) {
      AvRevertMmThreadCharacteristics(task);
    }
    return;
  }

  while (!stoken.stop_requested()) {
    const HRESULT drain = DrainDeviceQueue(device, filter, stoken);
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
  if (task != nullptr) {
    AvRevertMmThreadCharacteristics(task);
  }
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

  if (const AudioPipelineInterface::Status open = device_->Open();
      !open.ok()) {
    return open;
  }

  endpoint_name_ = device_->endpoint_name();
  filter_.SetSampleRate(device_->sample_rate());
  running_.store(true);

  audio_thread_ = std::jthread([this](std::stop_token stoken) {
    RunDeviceAudioThreadLoop(*device_, filter_, running_, std::move(stoken));
  });
  return AudioPipelineInterface::Status::Ok();
}

void AudioPipeline::Stop() {
  audio_thread_.request_stop();
  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }
  device_->StopStreams();
  device_->Close();
  running_.store(false);
}
