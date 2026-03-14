// Windows dark/light mode detection and colour palette.

#ifndef WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_
#define WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_

#define WIN32_LEAN_AND_MEAN
#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>

namespace theme_manager {

// UI colour palette derived from the current dark or light theme policy.
struct Palette {
  // Window background behind all child controls and custom drawing.
  COLORREF background = 0;
  // Raised panel background for grouped content surfaces.
  COLORREF surface = 0;
  // Primary highlight colour for emphasis and interactive affordances.
  COLORREF accent = 0;
  // Default high-contrast text colour.
  COLORREF text = 0;
  // Secondary text colour for hints and low-emphasis labels.
  COLORREF text_muted = 0;
  // Trough colour behind the slider thumb.
  COLORREF slider_track = 0;
  // Drag handle colour for the boost slider.
  COLORREF slider_thumb = 0;
  // Fill colour for the equalizer bars.
  COLORREF eq_bar = 0;
  // Peak indicator colour for the equalizer bars.
  COLORREF eq_bar_peak = 0;
  // Grid colour behind the equalizer visualization.
  COLORREF grid_line = 0;
  // Outline colour for panels and control edges.
  COLORREF border = 0;
};

// Returns true if the system is currently in dark mode.
[[nodiscard]] bool IsDarkMode();

// Returns a colour palette for the supplied `dark_mode` state. When
// `dark_mode` is true, the palette uses the dark theme colours; otherwise it
// uses the light theme colours.
[[nodiscard]] Palette BuildPalette(bool dark_mode);

// Returns a colour palette matched to the current dark/light mode.
[[nodiscard]] Palette BuildPalette();

// Enables the immersive dark title bar on the top-level `hwnd` when dark mode
// is active; reverts `hwnd` to the default title bar otherwise.
void ApplyTitleBarTheme(HWND hwnd);

// Two colours that define an interpolation range.
struct ColorRange {
  // Colour returned when `blend` is 0.0.
  COLORREF from = 0;
  // Colour returned when `blend` is 1.0.
  COLORREF to = 0;
};

// Returns a colour linearly interpolated across `range`. `blend` is clamped to
// [0.0, 1.0], so 0.0 returns `range.from`, 1.0 returns `range.to`, and 0.5
// returns the midpoint between them.
[[nodiscard]] COLORREF BlendColor(ColorRange range, float blend);

}  // namespace theme_manager

#endif  // WIN32BASSBOOSTER_SRC_THEME_MANAGER_HPP_
