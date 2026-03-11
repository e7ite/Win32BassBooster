// Audio capture and render pipeline.

#include "audio_pipeline.hpp"

// clang-format off
// initguid.h must precede functiondiscoverykeys_devpkey.h:
// `DEFINE_PROPERTYKEY` / `DEFINE_GUID` emit definitions only when `INITGUID`
// is set.
#include <initguid.h>
// clang-format on

#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <cmath>
#include <cstring>
#include <span>
#include <string_view>

#include "bass_boost_filter.hpp"
#include "endpoint_audio_format.hpp"
#include "harmonic_exciter.hpp"

namespace {

// 20 ms is the lowest buffer duration that avoids glitches on most Windows
// hardware while keeping latency perceptually invisible.
// WASAPI measures time in 100-nanosecond units; 20 ms = 200,000 units.
constexpr REFERENCE_TIME k20Ms = 200'000;

// 100-nanosecond units per millisecond; used to convert `k20Ms` to wall time.
constexpr REFERENCE_TIME kHundredNsPerMs = 10'000;

constexpr DWORD kPollsPerBuffer = 4;
// Poll the capture buffer at 1/4 of the buffer period so the queue stays
// drained without burning CPU. Must stay well below `k20Ms` or glitches occur.
constexpr DWORD kPollIntervalMs =
    static_cast<DWORD>(k20Ms / kHundredNsPerMs / kPollsPerBuffer);

template <typename T>
using ScopedComPtr = std::unique_ptr<T, AudioPipeline::ComRelease>;

using ScopedWaveFormat =
    std::unique_ptr<WAVEFORMATEX, AudioPipeline::CoTaskMemFreeDeleter>;

// Returns true when `failure` indicates the endpoint or session was
// invalidated but a fresh client pair on the current default device may
// succeed; returns false for all other failures.
[[nodiscard]] bool IsRecoverableStreamFailure(HRESULT failure) {
  // These errors indicate the current audio endpoint/session became invalid but
  // may succeed after reopening clients on the current default endpoint.
  return failure == AUDCLNT_E_DEVICE_INVALIDATED ||
         failure == AUDCLNT_E_RESOURCES_INVALIDATED ||
         failure == AUDCLNT_E_SERVICE_NOT_RUNNING;
}

// Stops both audio clients, clears the running flag, and reverts the MMCSS
// thread priority boost.
void StopClientsAndFinalizeThread(IAudioClient& capture_audio_client,
                                  IAudioClient& render_audio_client,
                                  std::atomic<bool>& running, HANDLE task) {
  capture_audio_client.Stop();
  render_audio_client.Stop();
  running.store(false);
  if (task == nullptr) {
    return;
  }
  AvRevertMmThreadCharacteristics(task);
}

// Clears the running flag and reverts MMCSS thread priority. Does not stop
// audio clients; use when clients are already null or stopped.
void FinalizeThread(std::atomic<bool>& running, HANDLE task) {
  running.store(false);
  if (task == nullptr) {
    return;
  }
  AvRevertMmThreadCharacteristics(task);
}

// Stops both clients and finalizes the thread when both are non-null;
// falls back to `FinalizeThread` when either client is null.
void StopClientsAndFinalizeIfReady(IAudioClient* capture_audio_client,
                                   IAudioClient* render_audio_client,
                                   std::atomic<bool>& running, HANDLE task) {
  if (capture_audio_client == nullptr || render_audio_client == nullptr) {
    FinalizeThread(running, task);
    return;
  }
  StopClientsAndFinalizeThread(*capture_audio_client, *render_audio_client,
                               running, task);
}

// Releases the current capture buffer and writes the next pending packet size
// into `packet_size`. Returns `S_OK` on success; otherwise returns the first
// failing HRESULT.
[[nodiscard]] HRESULT ReleaseAndQueryNextPacket(
    IAudioCaptureClient& audio_capture_client, UINT32 frames,
    UINT32& packet_size) {
  if (const HRESULT release_capture_buffer =
          audio_capture_client.ReleaseBuffer(frames);
      FAILED(release_capture_buffer)) {
    return release_capture_buffer;
  }
  if (const HRESULT next_packet_size =
          audio_capture_client.GetNextPacketSize(&packet_size);
      FAILED(next_packet_size)) {
    return next_packet_size;
  }
  return S_OK;
}

// Runs the full DSP chain in-place: harmonic exciter, then bass boost filter,
// then per-sample tanh soft limiter.
void ApplyDspChain(std::span<float> pcm_buf, HarmonicExciter& exciter,
                   BassBoostFilter& filter) {
  exciter.ProcessStereo(pcm_buf);
  filter.ProcessStereo(pcm_buf);
  // Soft-limit: tanh is transparent near 0, compresses above 0.7 without
  // hard-clipping; eliminates pops/crackle at max boost.
  for (float& sample : pcm_buf) {
    sample = std::tanh(sample);
  }
}

// Copies processed stereo float samples into the render client's buffer.
// Returns `S_OK` on success, `S_FALSE` when the render buffer is too full to
// accept the frames, or a failing HRESULT on error.
[[nodiscard]] HRESULT WriteProcessedToRender(
    std::span<const float> pcm_buf, IAudioClient& audio_client,
    IAudioRenderClient& audio_render_client) {
  const UINT32 frames = static_cast<UINT32>(pcm_buf.size() / 2);
  if (frames == 0) {
    return S_OK;
  }

  UINT32 render_buf_size = 0;
  UINT32 render_padding = 0;
  if (const HRESULT status = audio_client.GetBufferSize(&render_buf_size);
      FAILED(status)) {
    return status;
  }
  if (const HRESULT status = audio_client.GetCurrentPadding(&render_padding);
      FAILED(status)) {
    return status;
  }

  const UINT32 available = render_buf_size - render_padding;
  if (available < frames) {
    return S_FALSE;
  }

  BYTE* render_buf = nullptr;
  if (const HRESULT status = audio_render_client.GetBuffer(frames, &render_buf);
      FAILED(status)) {
    return status;
  }
  if (render_buf == nullptr) {
    audio_render_client.ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
    return E_POINTER;
  }

  std::memcpy(render_buf, pcm_buf.data(), pcm_buf.size_bytes());
  return audio_render_client.ReleaseBuffer(frames, /*dwFlags=*/0);
}

struct CapturedPacket {
  const BYTE* bytes;
  UINT32 frames;
  DWORD flags;
};

// Decodes one captured packet to stereo float, applies the DSP chain, and
// writes the result to the render client. Returns `S_OK` on success or when
// the packet is silent/empty; otherwise returns the failing HRESULT.
[[nodiscard]] HRESULT ProcessCapturedPacket(
    const CapturedPacket& packet, const WAVEFORMATEX& capture_format,
    HarmonicExciter& exciter, BassBoostFilter& filter,
    IAudioClient& audio_client, IAudioRenderClient& audio_render_client) {
  const bool should_process_packet =
      (packet.flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && packet.frames > 0;
  if (!should_process_packet) {
    return S_OK;
  }

  endpoint_audio_format::StereoPcmBuffer buf =
      endpoint_audio_format::DecodeToStereoFloat(packet.bytes, packet.frames,
                                                 capture_format);
  ApplyDspChain(buf.samples, exciter, filter);
  return WriteProcessedToRender(buf.samples, audio_client, audio_render_client);
}

// Returns `Ok` when the render mix format is packed float32 stereo (the only
// layout the render path can write directly); returns an error otherwise.
[[nodiscard]] AudioPipelineInterface::Status ValidateRenderMixFormat(
    const WAVEFORMATEX& render_format) {
  if (endpoint_audio_format::SupportsDirectStereoFloatCopy(render_format)) {
    return AudioPipelineInterface::Status::Ok();
  }
  return AudioPipelineInterface::Status::Error(
      AUDCLNT_E_UNSUPPORTED_FORMAT,
      L"Render mix format is not packed float32 stereo; "
      L"conversion path is unavailable");
}

// Initializes the audio client in shared mode for rendering. Returns `Ok` on
// success; otherwise returns the failing HRESULT.
[[nodiscard]] AudioPipelineInterface::Status InitializeRenderClient(
    IAudioClient& audio_client, WAVEFORMATEX* render_format) {
  if (const HRESULT initialize_render =
          audio_client.Initialize(AUDCLNT_SHAREMODE_SHARED, /*StreamFlags=*/0,
                                  /*hnsBufferDuration=*/k20Ms,
                                  /*hnsPeriodicity=*/0, render_format,
                                  /*audioSessionGuid=*/nullptr);
      FAILED(initialize_render)) {
    return AudioPipelineInterface::Status::Error(
        initialize_render, L"IAudioClient::Initialize (render) failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

// Initializes the audio client in shared loopback mode for capture. Returns
// `Ok` on success; otherwise returns the failing HRESULT.
[[nodiscard]] AudioPipelineInterface::Status InitializeCaptureClient(
    IAudioClient& audio_client, WAVEFORMATEX* capture_format) {
  if (const HRESULT initialize_capture = audio_client.Initialize(
          AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
          /*hnsBufferDuration=*/k20Ms,
          /*hnsPeriodicity=*/0, capture_format,
          /*audioSessionGuid=*/nullptr);
      FAILED(initialize_capture)) {
    return AudioPipelineInterface::Status::Error(
        initialize_capture, L"IAudioClient::Initialize (loopback) failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

// Queries the `IAudioCaptureClient` service from an initialized audio client.
// Returns `Ok` on success; otherwise returns the failing HRESULT.
[[nodiscard]] AudioPipelineInterface::Status AcquireCaptureClientService(
    IAudioClient& audio_client,
    IAudioCaptureClient*& raw_audio_capture_client) {
  // COM expects a generic void** out-parameter. This cast is safe because
  // `GetService` only writes an interface pointer value for the requested IID
  // into `raw_client`; it does not dereference through void**.
  if (const HRESULT capture_service = audio_client.GetService(
          __uuidof(IAudioCaptureClient),
          reinterpret_cast<void**>(&raw_audio_capture_client));
      FAILED(capture_service)) {
    return AudioPipelineInterface::Status::Error(
        capture_service, L"GetService IAudioCaptureClient failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

// Initializes the capture client in loopback mode and acquires its capture
// service in one step. Returns `Ok` on success; otherwise returns the first
// failing status.
[[nodiscard]] AudioPipelineInterface::Status InitializeCaptureStream(
    IAudioClient& audio_client, WAVEFORMATEX* capture_format,
    IAudioCaptureClient*& raw_audio_capture_client) {
  if (const AudioPipelineInterface::Status capture_init =
          InitializeCaptureClient(audio_client, capture_format);
      !capture_init.ok()) {
    return capture_init;
  }
  return AcquireCaptureClientService(audio_client, raw_audio_capture_client);
}

// Queries the `IAudioRenderClient` service from an initialized audio client.
// Returns `Ok` on success; otherwise returns the failing HRESULT.
[[nodiscard]] AudioPipelineInterface::Status AcquireRenderClientService(
    IAudioClient& audio_client, IAudioRenderClient*& raw_audio_render_client) {
  // COM expects a generic void** out-parameter. This cast is safe because
  // `GetService` only writes an interface pointer value for the requested IID
  // into `raw_client`; it does not dereference through void**.
  if (const HRESULT render_service = audio_client.GetService(
          __uuidof(IAudioRenderClient),
          reinterpret_cast<void**>(&raw_audio_render_client));
      FAILED(render_service)) {
    return AudioPipelineInterface::Status::Error(
        render_service, L"GetService IAudioRenderClient failed");
  }
  return AudioPipelineInterface::Status::Ok();
}

// Writes the friendly name of `render_device` into `endpoint_name`. Falls
// back to "Default Render Device" when the property store is unavailable or
// the name is empty.
void ReadEndpointName(IMMDevice* render_device, std::wstring& endpoint_name) {
  if (render_device == nullptr) {
    endpoint_name = L"Default Render Device";
    return;
  }

  IPropertyStore* raw_props = nullptr;
  if (FAILED(render_device->OpenPropertyStore(STGM_READ, &raw_props))) {
    endpoint_name = L"Default Render Device";
    return;
  }

  ScopedComPtr<IPropertyStore> props(raw_props);
  PROPVARIANT prop_variant;
  PropVariantInit(&prop_variant);
  if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &prop_variant)) &&
      prop_variant.vt == VT_LPWSTR) {
    endpoint_name = prop_variant.pwszVal;
  }
  PropVariantClear(&prop_variant);
  if (endpoint_name.empty()) {
    endpoint_name = L"Default Render Device";
  }
}

struct EndpointAcquisition {
  AudioPipelineInterface::Status status;
  ScopedComPtr<IMMDeviceEnumerator> enumerator;
  ScopedComPtr<IMMDevice> render_device;
  std::wstring endpoint_name;
};

// Returns the current default render endpoint and its friendly name. On
// failure, `status` carries the failing HRESULT and message; the remaining
// fields are empty.
[[nodiscard]] EndpointAcquisition AcquireEndpoint() {
  EndpointAcquisition endpoint;

  IMMDeviceEnumerator* raw_enum = nullptr;
  // COM expects a generic void** out-parameter. This cast is safe because
  // `CoCreateInstance` only writes an interface pointer value for the
  // requested IID into `raw_enum`; it does not dereference through void**.
  if (const HRESULT create_enumerator = CoCreateInstance(
          __uuidof(MMDeviceEnumerator),
          /*pUnkOuter=*/nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
          reinterpret_cast<void**>(&raw_enum));
      FAILED(create_enumerator)) {
    endpoint.status = AudioPipelineInterface::Status::Error(
        create_enumerator, L"CoCreateInstance(MMDeviceEnumerator) failed");
    return endpoint;
  }
  endpoint.enumerator.reset(raw_enum);

  IMMDevice* raw_device = nullptr;
  if (const HRESULT default_endpoint =
          endpoint.enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                       &raw_device);
      FAILED(default_endpoint)) {
    endpoint.status = AudioPipelineInterface::Status::Error(
        default_endpoint, L"GetDefaultAudioEndpoint failed");
    return endpoint;
  }
  endpoint.render_device.reset(raw_device);

  ReadEndpointName(endpoint.render_device.get(), endpoint.endpoint_name);
  return endpoint;
}

struct CaptureClientSetup {
  ScopedComPtr<IAudioClient> audio_client;
  ScopedComPtr<IAudioCaptureClient> audio_capture_client;
  ScopedWaveFormat capture_format;
};

// Activates a loopback capture client on `render_device`, queries the mix
// format, and initializes the capture stream. Populates `setup` on success;
// returns the first failing status otherwise.
[[nodiscard]] AudioPipelineInterface::Status SetupCaptureClient(
    IMMDevice& render_device, CaptureClientSetup& setup) {
  IAudioClient* raw_capture = nullptr;
  // COM expects a generic void** out-parameter. This cast is safe because
  // `Activate` only writes an interface pointer value for the requested IID
  // into `raw_capture`; it does not dereference through void**.
  if (const HRESULT activate_capture =
          render_device.Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                 /*pActivationParams=*/nullptr,
                                 reinterpret_cast<void**>(&raw_capture));
      FAILED(activate_capture)) {
    return AudioPipelineInterface::Status::Error(
        activate_capture, L"Activate capture IAudioClient failed");
  }
  setup.audio_client.reset(raw_capture);

  WAVEFORMATEX* raw_capture_format = nullptr;
  if (const HRESULT capture_mix_format =
          setup.audio_client->GetMixFormat(&raw_capture_format);
      FAILED(capture_mix_format)) {
    return AudioPipelineInterface::Status::Error(
        capture_mix_format, L"GetMixFormat (capture) failed");
  }
  setup.capture_format.reset(raw_capture_format);

  IAudioCaptureClient* raw_client = nullptr;
  if (const AudioPipelineInterface::Status capture_stream =
          InitializeCaptureStream(*setup.audio_client,
                                  setup.capture_format.get(), raw_client);
      !capture_stream.ok()) {
    return capture_stream;
  }
  setup.audio_capture_client.reset(raw_client);
  return AudioPipelineInterface::Status::Ok();
}

struct RenderClientSetup {
  ScopedComPtr<IAudioClient> audio_client;
  ScopedComPtr<IAudioRenderClient> audio_render_client;
  ScopedWaveFormat render_format;
};

// Activates a shared-mode render client on `render_device`, validates the mix
// format is float32 stereo, and initializes the render stream. Populates
// `setup` on success; returns the first failing status otherwise.
[[nodiscard]] AudioPipelineInterface::Status SetupRenderClient(
    IMMDevice& render_device, RenderClientSetup& setup) {
  IAudioClient* raw_render = nullptr;
  // COM expects a generic void** out-parameter. This cast is safe because
  // `Activate` only writes an interface pointer value for the requested IID
  // into `raw_render`; it does not dereference through void**.
  if (const HRESULT activate_render = render_device.Activate(
          __uuidof(IAudioClient), CLSCTX_ALL,
          /*pActivationParams=*/nullptr, reinterpret_cast<void**>(&raw_render));
      FAILED(activate_render)) {
    return AudioPipelineInterface::Status::Error(
        activate_render, L"Activate render IAudioClient failed");
  }
  setup.audio_client.reset(raw_render);

  WAVEFORMATEX* raw_render_format = nullptr;
  if (const HRESULT render_mix_format =
          setup.audio_client->GetMixFormat(&raw_render_format);
      FAILED(render_mix_format)) {
    return AudioPipelineInterface::Status::Error(
        render_mix_format, L"GetMixFormat (render) failed");
  }
  setup.render_format.reset(raw_render_format);
  if (const AudioPipelineInterface::Status format_check =
          ValidateRenderMixFormat(*setup.render_format);
      !format_check.ok()) {
    return format_check;
  }

  if (const AudioPipelineInterface::Status render_init = InitializeRenderClient(
          *setup.audio_client, setup.render_format.get());
      !render_init.ok()) {
    return render_init;
  }

  IAudioRenderClient* raw_audio_render_client = nullptr;
  if (const AudioPipelineInterface::Status render_service =
          AcquireRenderClientService(*setup.audio_client,
                                     raw_audio_render_client);
      !render_service.ok()) {
    return render_service;
  }
  setup.audio_render_client.reset(raw_audio_render_client);
  return AudioPipelineInterface::Status::Ok();
}

struct StreamClientSetup {
  CaptureClientSetup capture;
  RenderClientSetup render;
};

// Returns `Ok` when both capture and render clients are ready; returns the
// first failing status otherwise.
[[nodiscard]] AudioPipelineInterface::Status SetupStreamClients(
    IMMDevice& render_device, StreamClientSetup& clients) {
  if (const AudioPipelineInterface::Status capture =
          SetupCaptureClient(render_device, clients.capture);
      !capture.ok()) {
    return capture;
  }
  if (const AudioPipelineInterface::Status render =
          SetupRenderClient(render_device, clients.render);
      !render.ok()) {
    return render;
  }
  return AudioPipelineInterface::Status::Ok();
}

struct ActiveStreamState {
  IAudioClient* audio_client;
  IAudioCaptureClient* audio_capture_client;
  IAudioRenderClient* audio_render_client;
  WAVEFORMATEX* capture_format;
  HarmonicExciter& exciter;
  BassBoostFilter& filter;
};

// Processes all pending capture packets through the DSP chain and writes
// them to the render client. Returns `S_OK` when the queue is empty or stop
// was requested; otherwise returns the first failing HRESULT.
[[nodiscard]] HRESULT DrainCaptureQueue(ActiveStreamState& stream,
                                        std::stop_token stoken) {
  if (stream.audio_capture_client == nullptr ||
      stream.capture_format == nullptr || stream.audio_client == nullptr ||
      stream.audio_render_client == nullptr) {
    return E_POINTER;
  }

  UINT32 packet_size = 0;
  if (const HRESULT packet_size_query =
          stream.audio_capture_client->GetNextPacketSize(&packet_size);
      FAILED(packet_size_query)) {
    return packet_size_query;
  }

  while (packet_size > 0 && !stoken.stop_requested()) {
    BYTE* capture_bytes = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    if (const HRESULT status = stream.audio_capture_client->GetBuffer(
            &capture_bytes, &frames, &flags,
            /*device_position=*/nullptr, /*qpc_position=*/nullptr);
        FAILED(status)) {
      return status;
    }

    const CapturedPacket packet{
        .bytes = capture_bytes, .frames = frames, .flags = flags};
    if (const HRESULT process_packet = ProcessCapturedPacket(
            packet, *stream.capture_format, stream.exciter, stream.filter,
            *stream.audio_client, *stream.audio_render_client);
        FAILED(process_packet)) {
      stream.audio_capture_client->ReleaseBuffer(frames);
      return process_packet;
    }

    if (const HRESULT release_and_query = ReleaseAndQueryNextPacket(
            *stream.audio_capture_client, frames, packet_size);
        FAILED(release_and_query)) {
      return release_and_query;
    }
  }
  return S_OK;
}

struct RunningPipelineState {
  ScopedComPtr<IMMDeviceEnumerator>& enumerator;
  ScopedComPtr<IMMDevice>& render_device;
  ScopedComPtr<IAudioClient>& capture_audio_client;
  ScopedComPtr<IAudioClient>& render_audio_client;
  ScopedComPtr<IAudioCaptureClient>& audio_capture_client;
  ScopedComPtr<IAudioRenderClient>& audio_render_client;
  ScopedWaveFormat& capture_format;
  ScopedWaveFormat& render_format;
  BassBoostFilter& filter;
  HarmonicExciter& exciter;
  std::atomic<bool>& running;
  std::wstring& endpoint_name;
};

// Attempts to recover from a stream failure by re-acquiring the default
// endpoint and reopening both clients. Returns true when recovery succeeds
// and both streams are restarted; returns false otherwise.
[[nodiscard]] bool RecoverStreamFailure(RunningPipelineState& state,
                                        HRESULT failure,
                                        std::stop_token stoken) {
  if (stoken.stop_requested() || !IsRecoverableStreamFailure(failure)) {
    return false;
  }

  if (state.capture_audio_client == nullptr ||
      state.render_audio_client == nullptr) {
    return false;
  }

  state.capture_audio_client->Stop();
  state.render_audio_client->Stop();

  EndpointAcquisition endpoint = AcquireEndpoint();
  if (!endpoint.status.ok()) {
    return false;
  }

  StreamClientSetup clients;
  if (!SetupStreamClients(*endpoint.render_device, clients).ok()) {
    return false;
  }

  const double sample_rate =
      static_cast<double>(clients.capture.capture_format->nSamplesPerSec);
  state.filter.SetSampleRate(sample_rate);
  state.exciter.SetSampleRate(sample_rate);

  state.enumerator = std::move(endpoint.enumerator);
  state.render_device = std::move(endpoint.render_device);
  state.endpoint_name = std::move(endpoint.endpoint_name);
  state.capture_audio_client = std::move(clients.capture.audio_client);
  state.audio_capture_client = std::move(clients.capture.audio_capture_client);
  state.capture_format = std::move(clients.capture.capture_format);
  state.render_audio_client = std::move(clients.render.audio_client);
  state.audio_render_client = std::move(clients.render.audio_render_client);
  state.render_format = std::move(clients.render.render_format);

  if (FAILED(state.capture_audio_client->Start())) {
    return false;
  }

  return SUCCEEDED(state.render_audio_client->Start());
}

// Returns true when both capture and render streams are running; returns false
// when startup failed and recovery was unsuccessful.
[[nodiscard]] bool StartStreams(RunningPipelineState& state,
                                std::stop_token stoken) {
  const HRESULT capture_start = state.capture_audio_client->Start();
  if (FAILED(capture_start) &&
      !RecoverStreamFailure(state, capture_start, stoken)) {
    return false;
  }

  // S_OK when capture recovery already restarted both clients; calling
  // `Start()` a second time would fail because the stream is already running.
  const HRESULT render_start =
      FAILED(capture_start) ? S_OK : state.render_audio_client->Start();
  if (FAILED(render_start) &&
      !RecoverStreamFailure(state, render_start, stoken)) {
    return false;
  }

  return true;
}

// Audio thread entry point. Registers for MMCSS priority, starts both
// streams, and polls the capture queue until stop is requested or an
// unrecoverable failure occurs.
void RunAudioThreadLoop(RunningPipelineState& state, std::stop_token stoken) {
  DWORD task_index = 0;
  HANDLE task =
      AvSetMmThreadCharacteristicsW(/*taskName=*/L"Pro Audio", &task_index);

  if (state.capture_audio_client == nullptr ||
      state.render_audio_client == nullptr) {
    FinalizeThread(state.running, task);
    return;
  }

  if (!StartStreams(state, stoken)) {
    StopClientsAndFinalizeIfReady(state.capture_audio_client.get(),
                                  state.render_audio_client.get(),
                                  state.running, task);
    return;
  }

  while (!stoken.stop_requested()) {
    ActiveStreamState stream_state{
        .audio_client = state.render_audio_client.get(),
        .audio_capture_client = state.audio_capture_client.get(),
        .audio_render_client = state.audio_render_client.get(),
        .capture_format = state.capture_format.get(),
        .exciter = state.exciter,
        .filter = state.filter};

    const HRESULT drain = DrainCaptureQueue(stream_state, stoken);
    if (FAILED(drain) && !RecoverStreamFailure(state, drain, stoken)) {
      break;
    }

    if (FAILED(drain)) {
      continue;
    }

    Sleep(kPollIntervalMs);
  }

  StopClientsAndFinalizeIfReady(state.capture_audio_client.get(),
                                state.render_audio_client.get(), state.running,
                                task);
}

}  // namespace

AudioPipeline::AudioPipeline() = default;

AudioPipeline::~AudioPipeline() { Stop(); }

AudioPipelineInterface::Status AudioPipeline::Start() {
  if (running_.load()) {
    return AudioPipelineInterface::Status::Ok();
  }

  const bool needs_pipeline_init =
      enumerator_ == nullptr || render_device_ == nullptr;
  EndpointAcquisition endpoint;
  if (needs_pipeline_init) {
    endpoint = AcquireEndpoint();
    if (!endpoint.status.ok()) {
      return endpoint.status;
    }
  }

  IMMDevice& render_device =
      needs_pipeline_init ? *endpoint.render_device : *render_device_;

  StreamClientSetup clients;
  if (const AudioPipelineInterface::Status status =
          SetupStreamClients(render_device, clients);
      !status.ok()) {
    return status;
  }

  const double sample_rate =
      static_cast<double>(clients.capture.capture_format->nSamplesPerSec);
  filter_.SetSampleRate(sample_rate);
  exciter_.SetSampleRate(sample_rate);

  if (needs_pipeline_init) {
    enumerator_ = std::move(endpoint.enumerator);
    render_device_ = std::move(endpoint.render_device);
    endpoint_name_ = std::move(endpoint.endpoint_name);
  }
  capture_audio_client_ = std::move(clients.capture.audio_client);
  audio_capture_client_ = std::move(clients.capture.audio_capture_client);
  capture_format_ = std::move(clients.capture.capture_format);
  render_audio_client_ = std::move(clients.render.audio_client);
  audio_render_client_ = std::move(clients.render.audio_render_client);
  render_format_ = std::move(clients.render.render_format);

  running_.store(true);

  audio_thread_ = std::jthread([this](std::stop_token stoken) {
    RunningPipelineState state{.enumerator = enumerator_,
                               .render_device = render_device_,
                               .capture_audio_client = capture_audio_client_,
                               .render_audio_client = render_audio_client_,
                               .audio_capture_client = audio_capture_client_,
                               .audio_render_client = audio_render_client_,
                               .capture_format = capture_format_,
                               .render_format = render_format_,
                               .filter = filter_,
                               .exciter = exciter_,
                               .running = running_,
                               .endpoint_name = endpoint_name_};
    RunAudioThreadLoop(state, std::move(stoken));
  });
  return AudioPipelineInterface::Status::Ok();
}

void AudioPipeline::Stop() {
  audio_thread_.request_stop();
  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }
  StopClientsAndFinalizeIfReady(capture_audio_client_.get(),
                                render_audio_client_.get(), running_,
                                /*task=*/nullptr);
}
