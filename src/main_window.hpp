// Primary application window. Resizable and theme-aware, with a header panel,
// bass slider, and info footer.

#ifndef WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_
#define WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio_pipeline_interface.hpp"
#include "theme_manager.hpp"

// Cached layout geometry for the three fixed regions of the window: header,
// slider label row, and footer. Shared with the file-local paint helpers.
struct LayoutRegions {
  RECT header;
  RECT slider_label;
  RECT footer;
};

class MainWindow {
 public:
  // Does not take ownership; the pipeline must outlive the window.
  explicit MainWindow(AudioPipelineInterface* audio);

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  // Creates and shows the window using `instance` and `cmd_show`. Returns
  // false if Win32 window creation fails.
  [[nodiscard]] bool Create(HINSTANCE instance, int cmd_show);

  // Dispatches instance-specific window messages. Public so the free-function
  // `WNDPROC` callback in `main_window.cpp` can call it after looking up the
  // instance pointer from window user data. `hwnd` is this window's handle;
  // `msg`, `wparam`, and `lparam` are the raw Win32 message payload.
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
  [[nodiscard]] HWND slider_hwnd() const noexcept { return slider_hwnd_; }

 private:
  // The top-level application window that owns layout and painting.
  HWND hwnd_ = nullptr;
  // The slider is a separate Win32 child window; stored so the parent can
  // reposition it on resize and read its value on scroll events.
  HWND slider_hwnd_ = nullptr;
  // Module instance used when creating child windows and loading resources.
  HINSTANCE instance_ = nullptr;

  // Borrowed; owned by the caller. Must outlive the window.
  AudioPipelineInterface* audio_ = nullptr;
  // Rebuilt on every `WM_THEMECHANGED`/`WM_SETTINGCHANGE` so colors track the
  // system dark/light mode setting in real time.
  theme_manager::Palette palette_ = {};

  // Cached to avoid recomputing rectangles on every paint pass.
  LayoutRegions layout_ = {};
};

#endif  // WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_
