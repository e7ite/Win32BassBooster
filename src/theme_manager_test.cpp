// Verifies colour blending at various interpolation points, clamping at
// boundaries, channel independence, and that palette generation produces
// non-zero values for all fields on the current machine's colour scheme.

#include "theme_manager.hpp"

#include <windows.h>

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace theme_manager {
namespace {

using ::testing::Each;
using ::testing::Ne;

TEST(ThemeManagerTest, BlendColorAtT0ReturnsFrom) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(255, 255, 255)};
  const COLORREF blended = BlendColor(range, /*blend=*/0.0F);

  EXPECT_EQ(GetRValue(blended), 0U);
  EXPECT_EQ(GetGValue(blended), 0U);
  EXPECT_EQ(GetBValue(blended), 0U);
}

TEST(ThemeManagerTest, BlendColorAtT1ReturnsTo) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(200, 100, 50)};
  const COLORREF blended = BlendColor(range, /*blend=*/1.0F);

  EXPECT_EQ(GetRValue(blended), 200U);
  EXPECT_EQ(GetGValue(blended), 100U);
  EXPECT_EQ(GetBValue(blended), 50U);
}

TEST(ThemeManagerTest, BlendColorMidpointIsHalfway) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(100, 200, 50)};
  const COLORREF blended = BlendColor(range, /*blend=*/0.5F);

  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 50, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 100, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 25, 1);
}

TEST(ThemeManagerTest, BlendColorClampsAboveOne) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(255, 255, 255)};
  const COLORREF clamped = BlendColor(range, /*blend=*/2.0F);
  const COLORREF at_one = BlendColor(range, /*blend=*/1.0F);

  EXPECT_EQ(clamped, at_one);
}

TEST(ThemeManagerTest, BlendColorClampsBelowZero) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(255, 255, 255)};
  const COLORREF clamped = BlendColor(range, /*blend=*/-1.0F);
  const COLORREF at_zero = BlendColor(range, /*blend=*/0.0F);

  EXPECT_EQ(clamped, at_zero);
}

TEST(ThemeManagerTest, IsDarkModeDoesNotCrash) {
  static_cast<void>(IsDarkMode());
}

TEST(ThemeManagerTest, BuildPaletteReturnsNonZeroColors) {
  const Palette palette = BuildPalette();

  EXPECT_NE(palette.background + palette.surface + palette.text, 0U);
}

TEST(ThemeManagerTest, BuildPaletteDarkModeUsesDarkThemeColors) {
  const Palette palette = BuildPalette(/*dark_mode=*/true);

  EXPECT_EQ(palette.background, RGB(25, 25, 28));
  EXPECT_EQ(palette.surface, RGB(38, 38, 44));
  EXPECT_EQ(palette.accent, RGB(0, 120, 215));
  EXPECT_EQ(palette.text, RGB(240, 240, 240));
  EXPECT_EQ(palette.text_muted, RGB(160, 160, 160));
  EXPECT_EQ(palette.slider_track, RGB(70, 70, 78));
  EXPECT_EQ(palette.slider_thumb, RGB(0, 120, 215));
  EXPECT_EQ(palette.eq_bar, RGB(0, 140, 230));
  EXPECT_EQ(palette.eq_bar_peak, RGB(255, 80, 60));
  EXPECT_EQ(palette.grid_line, RGB(60, 60, 70));
  EXPECT_EQ(palette.border, RGB(60, 60, 68));
}

TEST(ThemeManagerTest, BuildPaletteLightModeUsesLightThemeColors) {
  const Palette palette = BuildPalette(/*dark_mode=*/false);

  EXPECT_EQ(palette.background, RGB(243, 243, 243));
  EXPECT_EQ(palette.surface, RGB(255, 255, 255));
  EXPECT_EQ(palette.accent, RGB(0, 103, 192));
  EXPECT_EQ(palette.text, RGB(28, 28, 28));
  EXPECT_EQ(palette.text_muted, RGB(100, 100, 100));
  EXPECT_EQ(palette.slider_track, RGB(190, 190, 200));
  EXPECT_EQ(palette.slider_thumb, RGB(0, 103, 192));
  EXPECT_EQ(palette.eq_bar, RGB(0, 120, 215));
  EXPECT_EQ(palette.eq_bar_peak, RGB(220, 40, 20));
  EXPECT_EQ(palette.grid_line, RGB(210, 210, 220));
  EXPECT_EQ(palette.border, RGB(210, 210, 220));
}

TEST(ThemeManagerTest, BuildPaletteAllFieldsNonZero) {
  const Palette palette = BuildPalette();
  const std::vector<COLORREF> fields = {
      palette.background,   palette.surface,    palette.accent,
      palette.text,         palette.text_muted, palette.slider_track,
      palette.slider_thumb, palette.eq_bar,     palette.eq_bar_peak,
      palette.grid_line,    palette.border};

  EXPECT_THAT(fields, Each(Ne(0U)));
}

TEST(ThemeManagerTest, BlendColorQuarterBlend) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(100, 200, 40)};
  const COLORREF blended = BlendColor(range, /*blend=*/0.25F);

  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 25, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 50, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 10, 1);
}

TEST(ThemeManagerTest, BlendColorThreeQuarterBlend) {
  const ColorRange range = {.from = RGB(0, 0, 0), .to = RGB(100, 200, 40)};
  const COLORREF blended = BlendColor(range, /*blend=*/0.75F);

  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 75, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 150, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 30, 1);
}

TEST(ThemeManagerTest, BlendColorIdenticalFromAndTo) {
  const COLORREF color = RGB(128, 64, 32);
  const ColorRange range = {.from = color, .to = color};

  // Any blend factor between identical colors should return the same color.
  EXPECT_EQ(BlendColor(range, /*blend=*/0.0F), color);
  EXPECT_EQ(BlendColor(range, /*blend=*/0.5F), color);
  EXPECT_EQ(BlendColor(range, /*blend=*/1.0F), color);
}

TEST(ThemeManagerTest, BlendColorChannelsDoNotCrossTalk) {
  // Only the red channel differs; green and blue should stay at `from`.
  const ColorRange range = {.from = RGB(0, 100, 200), .to = RGB(255, 100, 200)};
  const COLORREF blended = BlendColor(range, /*blend=*/0.5F);

  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 127, 1);
  EXPECT_EQ(GetGValue(blended), 100U);
  EXPECT_EQ(GetBValue(blended), 200U);
}

TEST(ThemeManagerTest, IsDarkModeReturnsBool) {
  // Can't control the system setting, but the return value should be
  // deterministic within a single call.
  const bool first = IsDarkMode();
  const bool second = IsDarkMode();

  EXPECT_EQ(first, second);
}

}  // namespace
}  // namespace theme_manager
