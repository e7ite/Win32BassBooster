// Application entry point.

#include <objbase.h>
#include <windows.h>

#include "audio_pipeline.hpp"
#include "main_window.hpp"

namespace {

// Programmatic DPI awareness as a belt-and-suspenders complement to the
// manifest declaration. Loaded at runtime so the binary still runs on older
// Windows versions that lack shcore.dll.
void EnableHighDpi() {
  using FnSetProcessDpiAwareness = HRESULT(WINAPI*)(int);
  HMODULE shcore = LoadLibraryW(L"shcore.dll");
  if (shcore == nullptr) {
    return;
  }

  auto set_dpi_fn = reinterpret_cast<FnSetProcessDpiAwareness>(
      GetProcAddress(shcore, "SetProcessDpiAwareness"));
  if (set_dpi_fn != nullptr) {
    set_dpi_fn(2);  // `PROCESS_PER_MONITOR_DPI_AWARE`
  }
  FreeLibrary(shcore);
}

void ShowFatalError(const wchar_t* message) {
  MessageBoxW(/*hWnd=*/nullptr, message, L"Fatal Error", MB_ICONERROR);
}

[[nodiscard]] std::unique_ptr<AudioPipeline> InitializeAudioPipeline() {
  auto audio_pipeline = std::make_unique<AudioPipeline>();
  const AudioPipelineInterface::Status status = audio_pipeline->Start();
  if (status.ok()) {
    return audio_pipeline;
  }

  if (status.error_message.empty()) {
    ShowFatalError(L"Audio startup failed.");
  } else {
    ShowFatalError(status.error_message.c_str());
  }
  return nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int nShowCmd) {
  EnableHighDpi();

  if (FAILED(CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED))) {
    ShowFatalError(L"COM initialisation failed.");
    return 1;
  }

  std::unique_ptr<AudioPipeline> audio_pipeline = InitializeAudioPipeline();
  if (audio_pipeline == nullptr) {
    CoUninitialize();
    return 1;
  }

  MainWindow wnd(std::move(audio_pipeline));
  if (!wnd.Create(hInstance, nShowCmd)) {
    ShowFatalError(L"Failed to create main window.");
    CoUninitialize();
    return 1;
  }

  const int ret = wnd.Run();
  CoUninitialize();
  return ret;
}
