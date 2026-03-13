// Windows theme detection and colour palette construction.

#include "theme_manager.hpp"

#include <algorithm>

namespace {

// Registry path where Windows stores the AppsUseLightTheme DWORD (0 = dark).
constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion"
    L"\\Themes\\Personalize";

}  // namespace

namespace theme_manager {

bool IsDarkMode() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, /*ulOptions=*/0,
                    KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }

  DWORD apps_use_light_theme = 1;  // 0 = dark, 1 = light
  DWORD size = sizeof(DWORD);
  RegQueryValueExW(key, L"AppsUseLightTheme", /*lpReserved=*/nullptr,
                   /*lpType=*/nullptr,
                   reinterpret_cast<LPBYTE>(&apps_use_light_theme), &size);
  RegCloseKey(key);
  return apps_use_light_theme == 0;
}

Palette BuildPalette(bool dark_mode) {
  Palette palette = {};
  if (dark_mode) {
    palette.background = RGB(25, 25, 28);
    palette.surface = RGB(38, 38, 44);
    palette.accent = RGB(0, 120, 215);
    palette.text = RGB(240, 240, 240);
    palette.text_muted = RGB(160, 160, 160);
    palette.slider_track = RGB(70, 70, 78);
    palette.slider_thumb = RGB(0, 120, 215);
    palette.eq_bar = RGB(0, 140, 230);
    palette.eq_bar_peak = RGB(255, 80, 60);
    palette.grid_line = RGB(60, 60, 70);
    palette.border = RGB(60, 60, 68);
  } else {
    palette.background = RGB(243, 243, 243);
    palette.surface = RGB(255, 255, 255);
    palette.accent = RGB(0, 103, 192);
    palette.text = RGB(28, 28, 28);
    palette.text_muted = RGB(100, 100, 100);
    palette.slider_track = RGB(190, 190, 200);
    palette.slider_thumb = RGB(0, 103, 192);
    palette.eq_bar = RGB(0, 120, 215);
    palette.eq_bar_peak = RGB(220, 40, 20);
    palette.grid_line = RGB(210, 210, 220);
    palette.border = RGB(210, 210, 220);
  }
  return palette;
}

Palette BuildPalette() { return BuildPalette(IsDarkMode()); }

void ApplyTitleBarTheme(HWND hwnd) {
  // `DWMWA_USE_IMMERSIVE_DARK_MODE` = 20 (Windows 10 20H1+).
  constexpr DWORD kDwmwaDarkMode = 20;
  BOOL dark = IsDarkMode() ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, kDwmwaDarkMode, &dark, sizeof(dark));
}

COLORREF BlendColor(ColorRange range, float blend) {
  blend = std::clamp(blend, 0.0F, 1.0F);
  auto lerp_channel = [blend](int from_channel, int to_channel) -> DWORD {
    return static_cast<DWORD>(
        static_cast<float>(from_channel) +
        (static_cast<float>(to_channel - from_channel) * blend));
  };
  return RGB(lerp_channel(GetRValue(range.from), GetRValue(range.to)),
             lerp_channel(GetGValue(range.from), GetGValue(range.to)),
             lerp_channel(GetBValue(range.from), GetBValue(range.to)));
}

}  // namespace theme_manager
