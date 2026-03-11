// Synchronous process-loopback activation for WASAPI capture clients.

#include "loopback_capture_activation.hpp"

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <string>

namespace {

// Synchronous wrapper for `ActivateAudioInterfaceAsync`. Signals an event when
// the activation completes so the calling thread can block until the audio
// client is ready. COM ref-counted: must be allocated with `new`.
class LoopbackActivationHandler
    : public IActivateAudioInterfaceCompletionHandler {
 public:
  LoopbackActivationHandler()
      : event_(CreateEventW(/*lpEventAttributes=*/nullptr,
                            /*bManualReset=*/TRUE,
                            /*bInitialState=*/FALSE,
                            /*lpName=*/nullptr)) {}

  LoopbackActivationHandler(const LoopbackActivationHandler&) = delete;
  LoopbackActivationHandler& operator=(const LoopbackActivationHandler&) =
      delete;

  // Blocks until the async activation completes. Returns `S_OK` when the
  // audio client was successfully activated; otherwise returns the failing
  // HRESULT from the activation.
  [[nodiscard]] HRESULT WaitForCompletion() {
    WaitForSingleObject(event_, INFINITE);
    return activate_hr_;
  }

  // Returns the activated audio client and releases ownership. Only valid
  // after `WaitForCompletion` returns `S_OK`.
  [[nodiscard]] IAudioClient* TakeAudioClient() {
    IAudioClient* client = audio_client_;
    audio_client_ = nullptr;
    return client;
  }

  // IActivateAudioInterfaceCompletionHandler
  HRESULT STDMETHODCALLTYPE
  ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
    IUnknown* activated = nullptr;
    activate_hr_ = E_FAIL;
    if (const HRESULT get_result =
            operation->GetActivateResult(&activate_hr_, &activated);
        FAILED(get_result)) {
      activate_hr_ = get_result;
      SetEvent(event_);
      return S_OK;
    }

    if (SUCCEEDED(activate_hr_) && activated != nullptr) {
      activated->QueryInterface(__uuidof(IAudioClient),
                                reinterpret_cast<void**>(&audio_client_));
      activated->Release();
    }
    SetEvent(event_);
    return S_OK;
  }

  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&ref_count_);
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
      delete this;
    }
    return count;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    // IAgileObject tells COM this handler can be called from any apartment
    // without marshaling. ActivateAudioInterfaceAsync requires it; without
    // it the async call fails because COM cannot marshal the callback.
    if (IsEqualIID(riid, __uuidof(IUnknown)) ||
        IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler)) ||
        IsEqualIID(riid, __uuidof(IAgileObject))) {
      *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

 private:
  ~LoopbackActivationHandler() {
    if (event_ != nullptr) {
      CloseHandle(event_);
    }
    if (audio_client_ != nullptr) {
      audio_client_->Release();
    }
  }

  HANDLE event_;
  HRESULT activate_hr_ = E_FAIL;
  IAudioClient* audio_client_ = nullptr;
  ULONG ref_count_ = 1;
};

[[nodiscard]] AudioPipelineInterface::Status MakeActivationErrorStatus(
    HRESULT status_code, const wchar_t* prefix) {
  constexpr int kHresultHexBufferChars = 9;

  wchar_t hex[kHresultHexBufferChars] = {};
  _snwprintf_s(hex, _TRUNCATE, L"%08lX",
               static_cast<unsigned long>(status_code));

  std::wstring message = prefix;
  message += hex;
  message += L')';
  return AudioPipelineInterface::Status::Error(status_code, std::move(message));
}

}  // namespace

AudioPipelineInterface::Status ActivateLoopbackCaptureClient(
    IAudioClient*& capture_client) {
  auto* handler = new LoopbackActivationHandler();

  AUDIOCLIENT_ACTIVATION_PARAMS activation_params = {};
  activation_params.ActivationType =
      AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  activation_params.ProcessLoopbackParams.TargetProcessId =
      GetCurrentProcessId();
  activation_params.ProcessLoopbackParams.ProcessLoopbackMode =
      PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

  PROPVARIANT activate_prop = {};
  activate_prop.vt = VT_BLOB;
  activate_prop.blob.cbSize = sizeof(activation_params);
  activate_prop.blob.pBlobData = reinterpret_cast<BYTE*>(&activation_params);

  IActivateAudioInterfaceAsyncOperation* async_op = nullptr;
  const HRESULT async_hr = ActivateAudioInterfaceAsync(
      VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
      &activate_prop, handler, &async_op);

  if (FAILED(async_hr)) {
    handler->Release();
    return MakeActivationErrorStatus(async_hr,
                                     L"ActivateAudioInterfaceAsync failed (0x");
  }

  const HRESULT activate_hr = handler->WaitForCompletion();
  capture_client =
      SUCCEEDED(activate_hr) ? handler->TakeAudioClient() : nullptr;
  handler->Release();
  async_op->Release();

  if (FAILED(activate_hr)) {
    return MakeActivationErrorStatus(activate_hr,
                                     L"Loopback capture activation failed (0x");
  }

  return AudioPipelineInterface::Status::Ok();
}
