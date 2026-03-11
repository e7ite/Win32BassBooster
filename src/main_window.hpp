// Primary application window. Resizable and theme-aware, with a header panel,
// bass slider, and info footer.

#ifndef WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_
#define WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>

#include "audio_pipeline_interface.hpp"
#include "theme_manager.hpp"

class MainWindow {
 public:
  // Accepts a pre-built pipeline so tests can run without a real audio device.
  explicit MainWindow(std::unique_ptr<AudioPipelineInterface> engine);
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  // Creates and shows the window. Returns false if Win32 window creation fails.
  [[nodiscard]] bool Create(HINSTANCE instance, int cmd_show);

  // Runs the Win32 message loop until `WM_QUIT`. Returns the process exit code.
  [[nodiscard]] static int Run();

  [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
  [[nodiscard]] HWND slider_hwnd() const noexcept { return slider_hwnd_; }

 private:
  // Routes window messages to the instance associated with `hwnd`.
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);

  // Handles instance-specific window messages after dispatch from `WndProc`.
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  // Creates child controls and primes initial layout/state.
  void OnCreate(HWND hwnd);

  // Recomputes layout rectangles and child control positions.
  void OnSize(int width, int height);

  // Paints the full window using the current theme and cached layout.
  void OnPaint();

  // Maps slider movement to boost level and triggers repaint of affected areas.
  void OnHScroll(HWND ctrl);

  // Rebuilds theme colors and updates themed controls after system changes.
  void OnThemeChange();

  // Stops audio and posts quit to end the message loop.
  void OnDestroy();

  HWND hwnd_ = nullptr;
  HWND slider_hwnd_ = nullptr;
  // Module instance used when creating child windows and loading resources.
  HINSTANCE instance_ = nullptr;

  std::unique_ptr<AudioPipelineInterface> audio_;
  // Rebuilt on every `WM_THEMECHANGED`/`WM_SETTINGCHANGE` so colors track the
  // system dark/light mode setting in real time.
  theme_manager::Palette palette_ = {};

  // Cached layout geometry for partial invalidation; storing these avoids
  // recomputing rectangles on every paint pass.
  RECT header_rc_ = {};
  RECT footer_rc_ = {};
  RECT slider_label_rc_ = {};
};

#endif  // WIN32BASSBOOSTER_SRC_MAIN_WINDOW_HPP_
