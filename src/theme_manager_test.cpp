// Verifies colour blending at various interpolation points, clamping at
// boundaries, channel independence, and that palette generation produces
// non-zero values for all fields on the current machine's colour scheme.

#include "theme_manager.hpp"

#include <windows.h>

#include "gtest/gtest.h"

namespace theme_manager {
namespace {

TEST(ThemeManagerTest, BlendColorAtT0ReturnsBase) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(255, 255, 255);
  const COLORREF blended = BlendColor(base, target, 0.0F);
  EXPECT_EQ(GetRValue(blended), 0U);
  EXPECT_EQ(GetGValue(blended), 0U);
  EXPECT_EQ(GetBValue(blended), 0U);
}

TEST(ThemeManagerTest, BlendColorAtT1ReturnsTarget) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(200, 100, 50);
  const COLORREF blended = BlendColor(base, target, 1.0F);
  EXPECT_EQ(GetRValue(blended), 200U);
  EXPECT_EQ(GetGValue(blended), 100U);
  EXPECT_EQ(GetBValue(blended), 50U);
}

TEST(ThemeManagerTest, BlendColorMidpointIsHalfway) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(100, 200, 50);
  const COLORREF blended = BlendColor(base, target, 0.5F);
  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 50, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 100, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 25, 1);
}

TEST(ThemeManagerTest, BlendColorClampsAboveOne) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(255, 255, 255);
  const COLORREF clamped = BlendColor(base, target, 2.0F);
  const COLORREF at_one = BlendColor(base, target, 1.0F);
  EXPECT_EQ(clamped, at_one);
}

TEST(ThemeManagerTest, BlendColorClampsBelowZero) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(255, 255, 255);
  const COLORREF clamped = BlendColor(base, target, -1.0F);
  const COLORREF at_zero = BlendColor(base, target, 0.0F);
  EXPECT_EQ(clamped, at_zero);
}

TEST(ThemeManagerTest, IsDarkModeDoesNotCrash) {
  static_cast<void>(IsDarkMode());
}

TEST(ThemeManagerTest, BuildPaletteReturnsNonZeroColors) {
  const Palette palette = BuildPalette();
  EXPECT_NE(palette.background + palette.surface + palette.text, 0U);
}

TEST(ThemeManagerTest, BuildPaletteAllFieldsNonZero) {
  const Palette palette = BuildPalette();
  EXPECT_NE(palette.background, 0U);
  EXPECT_NE(palette.surface, 0U);
  EXPECT_NE(palette.accent, 0U);
  EXPECT_NE(palette.text, 0U);
  EXPECT_NE(palette.text_muted, 0U);
  EXPECT_NE(palette.slider_track, 0U);
  EXPECT_NE(palette.slider_thumb, 0U);
  EXPECT_NE(palette.eq_bar, 0U);
  EXPECT_NE(palette.eq_bar_peak, 0U);
  EXPECT_NE(palette.grid_line, 0U);
  EXPECT_NE(palette.border, 0U);
}

TEST(ThemeManagerTest, BlendColorQuarterBlend) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(100, 200, 40);
  const COLORREF blended = BlendColor(base, target, 0.25F);
  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 25, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 50, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 10, 1);
}

TEST(ThemeManagerTest, BlendColorThreeQuarterBlend) {
  const COLORREF base = RGB(0, 0, 0);
  const COLORREF target = RGB(100, 200, 40);
  const COLORREF blended = BlendColor(base, target, 0.75F);
  EXPECT_NEAR(static_cast<int>(GetRValue(blended)), 75, 1);
  EXPECT_NEAR(static_cast<int>(GetGValue(blended)), 150, 1);
  EXPECT_NEAR(static_cast<int>(GetBValue(blended)), 30, 1);
}

TEST(ThemeManagerTest, BlendColorIdenticalBaseAndTarget) {
  const COLORREF color = RGB(128, 64, 32);
  // Any blend factor between identical colors should return the same color.
  EXPECT_EQ(BlendColor(color, color, 0.0F), color);
  EXPECT_EQ(BlendColor(color, color, 0.5F), color);
  EXPECT_EQ(BlendColor(color, color, 1.0F), color);
}

TEST(ThemeManagerTest, BlendColorChannelsDoNotCrossTalk) {
  // Only the red channel differs; green and blue should stay at base.
  const COLORREF base = RGB(0, 100, 200);
  const COLORREF target = RGB(255, 100, 200);
  const COLORREF blended = BlendColor(base, target, 0.5F);
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
