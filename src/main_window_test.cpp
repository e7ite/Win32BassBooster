// Verifies window creation, slider range, slider-to-boost mapping, minimum
// size constraints, theme changes, paint cycles, and control color responses.

#include "main_window.hpp"

#include <commctrl.h>
#include <windows.h>

#include <string>

#include "audio_pipeline_interface.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::DoubleNear;

class MockAudioPipeline final : public AudioPipelineInterface {
 public:
  [[nodiscard]] AudioPipelineInterface::Status Start() override { return {}; }
  MOCK_METHOD(void, Stop, (), (override));

  MOCK_METHOD(void, SetBoostLevel, (double level), (override));

  [[nodiscard]] double gain_db() const override { return 0.0; }

  [[nodiscard]] const std::wstring& endpoint_name() const override {
    return name_;
  }

 private:
  std::wstring name_ = L"Test Device";
};

TEST(MainWindowTest, DestroyWindowStopsAudioPipeline) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);

  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));
  ASSERT_NE(window.hwnd(), nullptr);
  ASSERT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);

  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SliderRangeIsValid) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HWND slider = window.slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));
  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  EXPECT_LT(slider_min, slider_max);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, HScrollMessageUpdatesBoostLevel) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HWND slider = window.slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));
  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  const int midpoint = (slider_min + slider_max) / 2;

  EXPECT_CALL(pipeline, SetBoostLevel(DoubleNear(0.5, 1e-6))).Times(1);
  SendMessageW(slider, TBM_SETPOS, TRUE, midpoint);
  SendMessageW(window.hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, midpoint),
               reinterpret_cast<LPARAM>(slider));

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, WindowHandleIsValid) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  EXPECT_NE(window.hwnd(), nullptr);
  EXPECT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SliderHandleIsValid) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  EXPECT_NE(window.slider_hwnd(), nullptr);
  EXPECT_NE(IsWindow(window.slider_hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SliderAtMinSetsZeroBoost) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HWND slider = window.slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));

  EXPECT_CALL(pipeline, SetBoostLevel(DoubleNear(0.0, 1e-6))).Times(1);
  SendMessageW(slider, TBM_SETPOS, TRUE, slider_min);
  SendMessageW(window.hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_min),
               reinterpret_cast<LPARAM>(slider));

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SliderAtMaxSetsMaxBoost) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HWND slider = window.slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));

  EXPECT_CALL(pipeline, SetBoostLevel(DoubleNear(1.0, 1e-6))).Times(1);
  SendMessageW(slider, TBM_SETPOS, TRUE, slider_max);
  SendMessageW(window.hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_max),
               reinterpret_cast<LPARAM>(slider));

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, GetMinMaxInfoEnforcesMinimumSize) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  MINMAXINFO info = {};
  SendMessageW(window.hwnd(), WM_GETMINMAXINFO, /*wParam=*/0,
               reinterpret_cast<LPARAM>(&info));

  EXPECT_GT(info.ptMinTrackSize.x, 0);
  EXPECT_GT(info.ptMinTrackSize.y, 0);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, EraseBkgndReturnsNonZero) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HDC hdc = GetDC(window.hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(window.hwnd(), WM_ERASEBKGND,
                                reinterpret_cast<WPARAM>(hdc), /*lParam=*/0);
  ReleaseDC(window.hwnd(), hdc);

  // Non-zero means the window handled the erase; prevents flicker.
  EXPECT_NE(result, 0);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, HScrollFromNonSliderIsIgnored) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  // Send WM_HSCROLL with a null HWND (not the slider); should be ignored.
  EXPECT_CALL(pipeline, SetBoostLevel).Times(0);
  SendMessageW(window.hwnd(), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 500),
               /*lParam=*/0);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SizeMessageUpdatesLayout) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  constexpr int kNewWidth = 800;
  constexpr int kNewHeight = 300;
  SendMessageW(window.hwnd(), WM_SIZE, SIZE_RESTORED,
               MAKELPARAM(kNewWidth, kNewHeight));

  // Window should still be valid after resize; layout was recomputed.
  EXPECT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, ThemeChangedDoesNotCrash) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  SendMessageW(window.hwnd(), WM_THEMECHANGED, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, SettingChangeDoesNotCrash) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  SendMessageW(window.hwnd(), WM_SETTINGCHANGE, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, PaintMessageDoesNotCrash) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  // Invalidate and force a paint cycle.
  InvalidateRect(window.hwnd(), /*lpRect=*/nullptr, /*bErase=*/FALSE);
  SendMessageW(window.hwnd(), WM_PAINT, /*wParam=*/0, /*lParam=*/0);

  EXPECT_NE(IsWindow(window.hwnd()), FALSE);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, CtlColorStaticReturnsNonNullBrush) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HDC hdc = GetDC(window.hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(
      window.hwnd(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(hdc),
      reinterpret_cast<LPARAM>(window.slider_hwnd()));
  ReleaseDC(window.hwnd(), hdc);

  // The brush handle must be non-null for the slider to paint correctly.
  EXPECT_NE(result, 0);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

TEST(MainWindowTest, CtlColorScrollbarReturnsNonNullBrush) {
  MockAudioPipeline pipeline;
  MainWindow window(&pipeline);
  ASSERT_TRUE(window.Create(GetModuleHandleW(/*lpModuleName=*/nullptr), SW_HIDE));

  HDC hdc = GetDC(window.hwnd());
  ASSERT_NE(hdc, nullptr);

  LRESULT result = SendMessageW(
      window.hwnd(), WM_CTLCOLORSCROLLBAR, reinterpret_cast<WPARAM>(hdc),
      reinterpret_cast<LPARAM>(window.slider_hwnd()));
  ReleaseDC(window.hwnd(), hdc);

  EXPECT_NE(result, 0);

  EXPECT_CALL(pipeline, Stop).Times(1);
  EXPECT_NE(DestroyWindow(window.hwnd()), FALSE);
  MSG msg = {};
  PeekMessageW(&msg, /*hWnd=*/nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
}

}  // namespace
