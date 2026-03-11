// Verifies the window and slider create successfully, slider range is valid,
// and slider positions at min, mid, and max map to the correct boost levels.

#include "main_window.hpp"

#include <commctrl.h>
#include <windows.h>

#include <memory>
#include <string>

#include "audio_pipeline_interface.hpp"
#include "gtest/gtest.h"

namespace {

class TestAudioPipeline : public AudioPipelineInterface {
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

// DestroyWindow posts teardown messages that can leak into the next test.
// In live Win32 tests there is no API that auto-isolates or auto-clears the
// thread message queue between `TEST` bodies; every approach (fixture or
// helper) must explicitly drain it to keep tests deterministic.
void DrainPendingMessages() {
  MSG msg = {};
  while (PeekMessageW(&msg, /*hWnd=*/nullptr, /*wMsgFilterMin=*/0,
                      /*wMsgFilterMax=*/0, PM_REMOVE) != FALSE) {
  }
}

class MainWindowLiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto audio_pipeline = std::make_unique<TestAudioPipeline>();
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
  TestAudioPipeline* pipeline_observer_ = nullptr;
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

TEST_F(MainWindowLiveTest, SliderAtMinSetsMaxBoost) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_min = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMIN, /*wParam=*/0, /*lParam=*/0));
  SendMessageW(slider, TBM_SETPOS, TRUE, slider_min);
  SendMessageW(window_->hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_min),
               reinterpret_cast<LPARAM>(slider));

  // Slider at min (leftmost) maps to maximum boost (level ~= 1.0).
  EXPECT_NEAR(pipeline_observer_->last_boost_level_, 1.0, 1e-6);
}

TEST_F(MainWindowLiveTest, SliderAtMaxSetsZeroBoost) {
  ASSERT_TRUE(window_created_);

  HWND slider = window_->slider_hwnd();
  ASSERT_NE(slider, nullptr);

  const int slider_max = static_cast<int>(
      SendMessageW(slider, TBM_GETRANGEMAX, /*wParam=*/0, /*lParam=*/0));
  SendMessageW(slider, TBM_SETPOS, TRUE, slider_max);
  SendMessageW(window_->hwnd(), WM_HSCROLL,
               MAKEWPARAM(TB_THUMBPOSITION, slider_max),
               reinterpret_cast<LPARAM>(slider));

  // Slider at max (rightmost) maps to zero boost (level ~= 0.0).
  EXPECT_NEAR(pipeline_observer_->last_boost_level_, 0.0, 1e-6);
}

}  // namespace
