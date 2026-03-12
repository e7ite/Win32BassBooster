// Verifies window creation, slider range, slider-to-boost mapping, minimum
// size constraints, theme changes, paint cycles, and control color responses.

#include "main_window.hpp"

#include <commctrl.h>
#include <windows.h>

#include <memory>
#include <string>

#include "audio_pipeline_interface.hpp"
#include "gtest/gtest.h"

namespace {

class MockAudioPipeline : public AudioPipelineInterface {
 public:
  [[nodiscard]] AudioPipelineInterface::Status Start() override { return {}; }
  void Stop() override {}

  void SetBoostLevel(double level) override {
    last_boost_level_ = level;
    ++set_level_count_;
  }

  [[nodiscard]] double gain_db() const override { return 0.0; }

  [[nodiscard]] const std::wstring& endpoint_name() const override {
    return name_;
  }

  double last_boost_level_ = -1.0;
  int set_level_count_ = 0;

 private:
  std::wstring name_ = L"Test Device";
};

// DestroyWindow posts teardown messages that can leak into the next test. In
// live Win32 tests there is no API that auto-isolates or auto-clears the thread
// message queue between `TEST` bodies; every approach (fixture or helper) must
// explicitly drain it to keep tests deterministic.
void DrainPendingMessages() {
  MSG msg = {};
  while (PeekMessageW(&msg, /*hWnd=*/nullptr, /*wMsgFilterMin=*/0,
                      /*wMsgFilterMax=*/0, PM_REMOVE) != FALSE) {
  }
}

class MainWindowLiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto audio_pipeline = std::make_unique<MockAudioPipeline>();
    pipeline_observer_ = audio_pipeline.get();
    window_ = std::make_unique<MainWindow>(std::move(audio_pipeline));
    window_created_ =
        window_->Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE);
  }

  void TearDown() override {
    const bool has_live_window = window_ != nullptr && window_created_;
    EXPECT_TRUE(has_live_window);

    HWND hwnd = has_live_window ? window_->hwnd() : nullptr;
    EXPECT_NE(hwnd, nullptr);
    EXPECT_NE(IsWindow(hwnd), FALSE);
    EXPECT_NE(DestroyWindow(hwnd), FALSE);

    DrainPendingMessages();
  }

  std::unique_ptr<MainWindow> window_;
  MockAudioPipeline* pipeline_observer_ = nullptr;
  bool window_created_ = false;
};

TEST_F(MainWindowLiveTest, SliderRangeIsValid) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));
  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  EXPECT_LT(slider_min, slider_max);
}

TEST_F(MainWindowLiveTest, HScrollMessageUpdatesBoostLevel) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));
  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  const int midpoint = (slider_min + slider_max) / 2;

  SendMessageW(slider, TBM_SETPOS, TRUE, midpoint);
  SendMessageW(window_->hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, midpoint),
               reinterpret_cast<LPARAM>(slider));

  EXPECT_EQ(pipeline_observer_->set_level_count_, 1);
  EXPECT_NEAR(pipeline_observer_->last_boost_level_, 0.5, 1e-6);
}

TEST_F(MainWindowLiveTest, WindowHandleIsValid) {
  ASSERT_TRUE(window_created_);
  EXPECT_NE(window_->hwnd(), nullptr);
  EXPECT_NE(IsWindow(window_->hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, SliderHandleIsValid) {
  ASSERT_TRUE(window_created_);
  EXPECT_NE(window_->slider_hwnd(), nullptr);
  EXPECT_NE(IsWindow(window_->slider_hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, SliderAtMinSetsZeroBoost) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));

  SendMessageW(slider, TBM_SETPOS, TRUE, slider_min);
  SendMessageW(window_->hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_min),
               reinterpret_cast<LPARAM>(slider));

  // Slider at min (leftmost) maps to zero boost (level ~= 0.0).
  EXPECT_NEAR(pipeline_observer_->last_boost_level_, 0.0, 1e-6);
}

TEST_F(MainWindowLiveTest, SliderAtMaxSetsMaxBoost) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  SendMessageW(slider, TBM_SETPOS, TRUE, slider_max);
  SendMessageW(window_->hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_max),
               reinterpret_cast<LPARAM>(slider));

  // Slider at max (rightmost) maps to maximum boost (level ~= 1.0).
  EXPECT_NEAR(pipeline_observer_->last_boost_level_, 1.0, 1e-6);
}

TEST_F(MainWindowLiveTest, GetMinMaxInfoEnforcesMinimumSize) {
  ASSERT_TRUE(window_created_);

  MINMAXINFO info = {};
  SendMessageW(window_->hwnd(), WM_GETMINMAXINFO, /*wParam=*/0,
               reinterpret_cast<LPARAM>(&info));

  EXPECT_GT(info.ptMinTrackSize.x, 0);
  EXPECT_GT(info.ptMinTrackSize.y, 0);
}

TEST_F(MainWindowLiveTest, EraseBkgndReturnsNonZero) {
  ASSERT_TRUE(window_created_);

  HDC hdc = GetDC(window_->hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(window_->hwnd(), WM_ERASEBKGND,
                                reinterpret_cast<WPARAM>(hdc), /*lParam=*/0);
  ReleaseDC(window_->hwnd(), hdc);

  // Non-zero means the window handled the erase; prevents flicker.
  EXPECT_NE(result, 0);
}

TEST_F(MainWindowLiveTest, HScrollFromNonSliderIsIgnored) {
  ASSERT_TRUE(window_created_);

  // Send WM_HSCROLL with a null HWND (not the slider); should be ignored.
  SendMessageW(window_->hwnd(), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 500),
               /*lParam=*/0);

  EXPECT_EQ(pipeline_observer_->set_level_count_, 0);
}

TEST_F(MainWindowLiveTest, SizeMessageUpdatesLayout) {
  ASSERT_TRUE(window_created_);

  constexpr int kNewWidth = 800;
  constexpr int kNewHeight = 300;
  SendMessageW(window_->hwnd(), WM_SIZE, SIZE_RESTORED,
               MAKELPARAM(kNewWidth, kNewHeight));

  // Window should still be valid after resize; layout was recomputed.
  EXPECT_NE(IsWindow(window_->hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, ThemeChangedDoesNotCrash) {
  ASSERT_TRUE(window_created_);

  SendMessageW(window_->hwnd(), WM_THEMECHANGED, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window_->hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, SettingChangeDoesNotCrash) {
  ASSERT_TRUE(window_created_);

  SendMessageW(window_->hwnd(), WM_SETTINGCHANGE, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window_->hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, PaintMessageDoesNotCrash) {
  ASSERT_TRUE(window_created_);

  // Invalidate and force a paint cycle.
  InvalidateRect(window_->hwnd(), /*lpRect=*/nullptr, /*bErase=*/FALSE);
  SendMessageW(window_->hwnd(), WM_PAINT, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window_->hwnd()), FALSE);
}

TEST_F(MainWindowLiveTest, CtlColorStaticReturnsNonNullBrush) {
  ASSERT_TRUE(window_created_);

  HDC hdc = GetDC(window_->hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(
      window_->hwnd(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(hdc),
      reinterpret_cast<LPARAM>(window_->slider_hwnd()));
  ReleaseDC(window_->hwnd(), hdc);

  // The brush handle must be non-null for the slider to paint correctly.
  EXPECT_NE(result, 0);
}

TEST_F(MainWindowLiveTest, CtlColorScrollbarReturnsNonNullBrush) {
  ASSERT_TRUE(window_created_);

  HDC hdc = GetDC(window_->hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(
      window_->hwnd(), WM_CTLCOLORSCROLLBAR, reinterpret_cast<WPARAM>(hdc),
      reinterpret_cast<LPARAM>(window_->slider_hwnd()));
  ReleaseDC(window_->hwnd(), hdc);

  EXPECT_NE(result, 0);
}

}  // namespace
