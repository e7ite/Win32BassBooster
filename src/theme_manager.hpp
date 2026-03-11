// Windows dark/light mode detection and colour palette.

#ifndef WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_
#define WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_

#define WIN32_LEAN_AND_MEAN
#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>

namespace theme_manager {

struct Palette {
  COLORREF background = 0;
  COLORREF surface = 0;
  COLORREF accent = 0;
  COLORREF text = 0;
  COLORREF text_muted = 0;
  COLORREF slider_track = 0;
  COLORREF slider_thumb = 0;
  COLORREF eq_bar = 0;
  COLORREF eq_bar_peak = 0;
  COLORREF grid_line = 0;
  COLORREF border = 0;
};

// Returns true if the system is currently in dark mode.
[[nodiscard]] bool IsDarkMode();

// Returns a colour palette matched to the current dark/light mode.
[[nodiscard]] Palette BuildPalette();

// Enables the immersive dark title bar on `hwnd` when dark mode is active;
// reverts to the default title bar otherwise.
void ApplyTitleBarTheme(HWND hwnd);

// Returns a colour linearly interpolated between `base` and `target`.
// `blend`=0.0 returns `base`; `blend`=1.0 returns `target`.
[[nodiscard]] COLORREF BlendColor(COLORREF base, COLORREF target, float blend);

}  // namespace theme_manager

#endif  // WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_
