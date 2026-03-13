// Audio capture and render pipeline.

#include "audio_pipeline.hpp"

// clang-format off
// initguid.h must precede functiondiscoverykeys_devpkey.h: `DEFINE_PROPERTYKEY`
// and `DEFINE_GUID` emit definitions only when `INITGUID` is set.
#include <initguid.h>
// clang-format on

#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <cstring>
#include <span>
#include <string_view>

#include "bass_boost_filter.hpp"
#include "endpoint_audio_format.hpp"
#include "loopback_capture_activation.hpp"
#include "wasapi_audio_device.hpp"

namespace {

// 20 ms is the lowest buffer duration that avoids glitches on most Windows
// hardware while keeping latency perceptually invisible. WASAPI measures time
// in 100-nanosecond units; 20 ms = 200,000 units.
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

// Returns true when `failure` indicates the endpoint or session was invalidated
// but a fresh client pair on the current default device may succeed; returns
// false for all other failures.
[[nodiscard]] bool IsRecoverableStreamFailure(HRESULT failure) {
  // These errors indicate the current audio endpoint/session became invalid
  // but may succeed after reopening clients on the current default endpoint.
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

// Stops both clients and finalizes the thread when both are non-null; falls
// back to `FinalizeThread` when either client is null.
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
// failing `HRESULT`.
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

// Decodes one captured packet to stereo float, applies only the bass boost
// filter, then subtracts the original so only the low-frequency energy the
// filter added is rendered. The low-shelf filter leaves highs unchanged, so
// filter(signal) - signal is zero above the shelf and positive in the bass
// range. This avoids comb filtering on mids/highs that would result from
// rendering a delayed full-band copy. Returns `S_OK` on success or when the
// packet is silent/empty; otherwise returns the failing HRESULT.
[[nodiscard]] HRESULT ProcessCapturedPacket(
    const CapturedPacket& packet, const WAVEFORMATEX& capture_format,
    BassBoostFilter& filter, IAudioClient& audio_client,
    IAudioRenderClient& audio_render_client) {
  const bool should_process_packet =
      (packet.flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && packet.frames > 0;
  if (!should_process_packet) {
    return S_OK;
  }

  endpoint_audio_format::StereoPcmBuffer buf =
      endpoint_audio_format::DecodeToStereoFloat(packet.bytes, packet.frames,
                                                 capture_format);

  const std::vector<float> original(buf.samples.begin(), buf.samples.end());

  // Apply only the filter -- no output attenuation. Process loopback prevents
  // feedback, so attenuation is unnecessary. The delta below isolates just the
  // bass energy the shelf added.
  filter.ProcessStereo(buf.samples);

  for (size_t i = 0; i < buf.samples.size(); ++i) {
    buf.samples[i] = std::clamp(buf.samples[i] - original[i], -1.0F, 1.0F);
  }

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

// Initializes the audio client in shared mode for loopback capture. The
// process loopback activation selects which audio is captured, but the stream
// flag `AUDCLNT_STREAMFLAGS_LOOPBACK` is still required. Buffer duration 0
// lets WASAPI choose the optimal size for process loopback. Returns `Ok` on
// success; otherwise returns the failing HRESULT.
[[nodiscard]] AudioPipelineInterface::Status InitializeCaptureClient(
    IAudioClient& audio_client, WAVEFORMATEX* capture_format) {
  if (const HRESULT initialize_capture = audio_client.Initialize(
          AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
          /*hnsBufferDuration=*/0,
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

// Initializes the capture client and acquires its capture service in one step.
// Returns `Ok` on success; otherwise returns the first failing status.
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

// Writes the friendly name of `render_device` into `endpoint_name`. Falls back
// to "Default Render Device" when the property store is unavailable or the name
// is empty.
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
  // `CoCreateInstance` only writes an interface pointer value for the requested
  // IID into `raw_enum`; it does not dereference through void**.
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

// WASAPI loopback capture client and its packet-pulling service, paired so
// they can be set up together and moved into the pipeline as a unit.
struct CaptureClientSetup {
  // Owns the loopback stream configuration and lifetime.
  ScopedComPtr<IAudioClient> audio_client;
  // Pulls captured packets from `audio_client`.
  ScopedComPtr<IAudioCaptureClient> service;
};

// Activates a loopback capture client using the process loopback API and
// initializes it with `render_format`. The process loopback virtual device
// has no mix format of its own; it captures in whatever format the render
// endpoint uses. Populates `setup` on success; returns the first failing
// status otherwise.
[[nodiscard]] AudioPipelineInterface::Status SetupCaptureClient(
    WAVEFORMATEX* render_format, CaptureClientSetup& setup) {
  IAudioClient* raw_capture = nullptr;
  if (const AudioPipelineInterface::Status activate =
          ActivateLoopbackCaptureClient(raw_capture);
      !activate.ok()) {
    return activate;
  }
  setup.audio_client.reset(raw_capture);

  IAudioCaptureClient* raw_client = nullptr;
  if (const AudioPipelineInterface::Status capture_stream =
          InitializeCaptureStream(*setup.audio_client, render_format,
                                  raw_client);
      !capture_stream.ok()) {
    return capture_stream;
  }
  setup.service.reset(raw_client);
  return AudioPipelineInterface::Status::Ok();
}

// WASAPI render client, its buffer-writing service, and the negotiated mix
// format, paired so they can be set up together and moved into the pipeline
// as a unit.
struct RenderClientSetup {
  // Owns the render stream configuration and lifetime.
  ScopedComPtr<IAudioClient> audio_client;
  // Writes processed frames into the render buffer owned by `audio_client`.
  ScopedComPtr<IAudioRenderClient> service;
  // Negotiated mix format; also used as the capture format because process
  // loopback captures in whatever format the render endpoint uses.
  ScopedWaveFormat format;
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
  setup.format.reset(raw_render_format);
  if (const AudioPipelineInterface::Status format_check =
          ValidateRenderMixFormat(*setup.format);
      !format_check.ok()) {
    return format_check;
  }

  if (const AudioPipelineInterface::Status render_init =
          InitializeRenderClient(*setup.audio_client, setup.format.get());
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
  setup.service.reset(raw_audio_render_client);
  return AudioPipelineInterface::Status::Ok();
}

// Combined capture and render setup, built during startup and recovery then
// moved into the pipeline's long-lived members.
struct StreamClientSetup {
  CaptureClientSetup capture;
  RenderClientSetup render;
};

// Sets up the render client first (to obtain the mix format), then activates
// the process loopback capture client using that same format. Returns `Ok`
// when both clients are ready; returns the first failing status otherwise.
[[nodiscard]] AudioPipelineInterface::Status SetupStreamClients(
    IMMDevice& render_device, StreamClientSetup& clients) {
  if (const AudioPipelineInterface::Status render =
          SetupRenderClient(render_device, clients.render);
      !render.ok()) {
    return render;
  }
  if (const AudioPipelineInterface::Status capture =
          SetupCaptureClient(clients.render.format.get(), clients.capture);
      !capture.ok()) {
    return capture;
  }
  return AudioPipelineInterface::Status::Ok();
}

// Non-owning snapshot of the resources needed by one drain pass. Assembled
// from `RunningPipelineState` each iteration so `DrainCaptureQueue` has no
// dependency on the pipeline's ownership model.
struct ActiveStreamState {
  // Queried for render buffer size before writing processed frames.
  IAudioClient* render_client;
  // Pulls captured packets from the loopback stream.
  IAudioCaptureClient* capture_service;
  // Writes processed frames into the render buffer.
  IAudioRenderClient* render_service;
  // Render mix format reused as the capture format; process loopback
  // captures in whatever format the render endpoint uses.
  WAVEFORMATEX* format;
  BassBoostFilter& filter;
};

// Processes all pending capture packets through the DSP chain and writes them
// to the render client. Returns `S_OK` when the queue is empty or stop was
// requested; otherwise returns the first failing HRESULT.
[[nodiscard]] HRESULT DrainCaptureQueue(ActiveStreamState& stream,
                                        std::stop_token stoken) {
  if (stream.capture_service == nullptr || stream.format == nullptr ||
      stream.render_client == nullptr || stream.render_service == nullptr) {
    return E_POINTER;
  }

  UINT32 packet_size = 0;
  if (const HRESULT packet_size_query =
          stream.capture_service->GetNextPacketSize(&packet_size);
      FAILED(packet_size_query)) {
    return packet_size_query;
  }

  while (packet_size > 0 && !stoken.stop_requested()) {
    BYTE* capture_bytes = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    if (const HRESULT status = stream.capture_service->GetBuffer(
            &capture_bytes, &frames, &flags,
            /*device_position=*/nullptr, /*qpc_position=*/nullptr);
        FAILED(status)) {
      return status;
    }

    const CapturedPacket packet{
        .bytes = capture_bytes, .frames = frames, .flags = flags};
    if (const HRESULT process_packet = ProcessCapturedPacket(
            packet, *stream.format, stream.filter, *stream.render_client,
            *stream.render_service);
        FAILED(process_packet)) {
      stream.capture_service->ReleaseBuffer(frames);
      return process_packet;
    }

    if (const HRESULT release_and_query = ReleaseAndQueryNextPacket(
            *stream.capture_service, frames, packet_size);
        FAILED(release_and_query)) {
      return release_and_query;
    }
  }
  return S_OK;
}

// Mutable references to every `AudioPipeline` member the audio thread needs.
// Passed into the thread so recovery can swap in fresh clients without the
// thread holding a back-pointer to the pipeline object.
struct RunningPipelineState {
  ScopedComPtr<IMMDeviceEnumerator>& enumerator;
  ScopedComPtr<IMMDevice>& render_device;
  ScopedComPtr<IAudioClient>& capture_client;
  ScopedComPtr<IAudioClient>& render_client;
  ScopedComPtr<IAudioCaptureClient>& capture_service;
  ScopedComPtr<IAudioRenderClient>& render_service;
  // Render mix format reused as the capture format; process loopback
  // captures in whatever format the render endpoint uses.
  ScopedWaveFormat& render_format;
  BassBoostFilter& filter;
  std::atomic<bool>& running;
  std::wstring& endpoint_name;
};

// Attempts to recover from a stream failure by re-acquiring the default
// endpoint and reopening both clients. Returns true when recovery succeeds and
// both streams are restarted; returns false otherwise.
[[nodiscard]] bool RecoverStreamFailure(RunningPipelineState& state,
                                        HRESULT failure,
                                        std::stop_token stoken) {
  if (stoken.stop_requested() || !IsRecoverableStreamFailure(failure)) {
    return false;
  }

  if (state.capture_client == nullptr || state.render_client == nullptr) {
    return false;
  }

  state.capture_client->Stop();
  state.render_client->Stop();

  EndpointAcquisition endpoint = AcquireEndpoint();
  if (!endpoint.status.ok()) {
    return false;
  }

  StreamClientSetup clients;
  if (!SetupStreamClients(*endpoint.render_device, clients).ok()) {
    return false;
  }

  const double sample_rate =
      static_cast<double>(clients.render.format->nSamplesPerSec);
  state.filter.SetSampleRate(sample_rate);

  state.enumerator = std::move(endpoint.enumerator);
  state.render_device = std::move(endpoint.render_device);
  state.endpoint_name = std::move(endpoint.endpoint_name);
  state.capture_client = std::move(clients.capture.audio_client);
  state.capture_service = std::move(clients.capture.service);
  state.render_client = std::move(clients.render.audio_client);
  state.render_service = std::move(clients.render.service);
  state.render_format = std::move(clients.render.format);

  if (FAILED(state.capture_client->Start())) {
    return false;
  }

  return SUCCEEDED(state.render_client->Start());
}

// Returns true when both capture and render streams are running; returns false
// when startup failed and recovery was unsuccessful.
[[nodiscard]] bool StartStreams(RunningPipelineState& state,
                                std::stop_token stoken) {
  const HRESULT capture_start = state.capture_client->Start();
  if (FAILED(capture_start) &&
      !RecoverStreamFailure(state, capture_start, stoken)) {
    return false;
  }

  // S_OK when capture recovery already restarted both clients; calling
  // `Start()` a second time would fail because the stream is already running.
  const HRESULT render_start =
      FAILED(capture_start) ? S_OK : state.render_client->Start();
  return SUCCEEDED(render_start) ||
         RecoverStreamFailure(state, render_start, stoken);
}

// Audio thread entry point. Registers for MMCSS priority, starts both streams,
// and polls the capture queue until stop is requested or an unrecoverable
// failure occurs.
void RunAudioThreadLoop(RunningPipelineState& state, std::stop_token stoken) {
  DWORD task_index = 0;
  HANDLE task =
      AvSetMmThreadCharacteristicsW(/*taskName=*/L"Pro Audio", &task_index);

  if (state.capture_client == nullptr || state.render_client == nullptr) {
    FinalizeThread(state.running, task);
    return;
  }

  if (!StartStreams(state, stoken)) {
    StopClientsAndFinalizeIfReady(state.capture_client.get(),
                                  state.render_client.get(), state.running,
                                  task);
    return;
  }

  while (!stoken.stop_requested()) {
    ActiveStreamState stream_state{
        .render_client = state.render_client.get(),
        .capture_service = state.capture_service.get(),
        .render_service = state.render_service.get(),
        .format = state.render_format.get(),
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

  StopClientsAndFinalizeIfReady(state.capture_client.get(),
                                state.render_client.get(), state.running, task);
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
      static_cast<double>(clients.render.format->nSamplesPerSec);
  filter_.SetSampleRate(sample_rate);

  if (needs_pipeline_init) {
    enumerator_ = std::move(endpoint.enumerator);
    render_device_ = std::move(endpoint.render_device);
    endpoint_name_ = std::move(endpoint.endpoint_name);
  }
  capture_client_ = std::move(clients.capture.audio_client);
  capture_service_ = std::move(clients.capture.service);
  render_client_ = std::move(clients.render.audio_client);
  render_service_ = std::move(clients.render.service);
  render_format_ = std::move(clients.render.format);

  running_.store(true);

  audio_thread_ = std::jthread([this](std::stop_token stoken) {
    RunningPipelineState state{.enumerator = enumerator_,
                               .render_device = render_device_,
                               .capture_client = capture_client_,
                               .render_client = render_client_,
                               .capture_service = capture_service_,
                               .render_service = render_service_,
                               .render_format = render_format_,
                               .filter = filter_,
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
  StopClientsAndFinalizeIfReady(capture_client_.get(), render_client_.get(),
                                running_,
                                /*task=*/nullptr);
}
