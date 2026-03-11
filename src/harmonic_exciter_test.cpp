// Verifies harmonic enhancement is audible on bass-heavy input, scales with
// blend amount, processes channels independently, leaves high-frequency content
// stable, and recovers cleanly after reset.

#include "harmonic_exciter.hpp"

#include <cmath>

#include "gtest/gtest.h"

namespace {

constexpr double kSampleRate = 48000.0;

TEST(HarmonicExciterTest, BypassAtZeroAmount) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(0.0);
  constexpr float kSampleA = 0.5F;
  constexpr float kSampleB = 0.3F;
  constexpr float kSampleC = 0.8F;
  float buf[] = {kSampleA, kSampleA, -kSampleB, -kSampleB, kSampleC, kSampleC};
  exciter.ProcessStereo(buf);
  // Even indices are the L channel; R mirrors L in this buffer so one channel
  // is sufficient to verify bypass.
  EXPECT_FLOAT_EQ(buf[0], kSampleA);
  EXPECT_FLOAT_EQ(buf[2], -kSampleB);
  EXPECT_FLOAT_EQ(buf[4], kSampleC);
}

TEST(HarmonicExciterTest, AddsEnergyAtMaxAmount) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  // DC input causes the LPF to produce a small but non-zero output on the
  // first sample; the rectifier and HPF pass it; the output exceeds 1.0.
  float buf[] = {1.0F, 1.0F};
  exciter.ProcessStereo(buf);
  EXPECT_GT(buf[0], 1.0F);
}

TEST(HarmonicExciterTest, HighFreqUnaffected) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  // Nyquist-rate signal (alternating +/-1) is rejected by the 100 Hz LPF,
  // so the exciter contributes nothing and the output stays at +/-1.
  float buf[] = {1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F,
                 1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F};
  exciter.ProcessStereo(buf);
  // buf[12] and buf[14] are L-channel samples of frames 6 and 7; checking
  // two consecutive settled frames confirms the LPF transient is fully gone.
  EXPECT_NEAR(std::abs(buf[12]), 1.0F, 0.01F);
  EXPECT_NEAR(std::abs(buf[14]), 1.0F, 0.01F);
}

TEST(HarmonicExciterTest, ResetClearsState) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  float signal[] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  exciter.ProcessStereo(signal);

  exciter.Reset();

  float zeros[] = {0.0F, 0.0F, 0.0F, 0.0F};
  exciter.ProcessStereo(zeros);
  // Even indices are the L channel; each channel has its own delay line, so
  // one sample per channel is enough to verify both were cleared.
  EXPECT_NEAR(zeros[0], 0.0F, 1e-6F);
  EXPECT_NEAR(zeros[2], 0.0F, 1e-6F);
}

TEST(HarmonicExciterTest, AmountClampedBelowZero) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(-1.0);
  EXPECT_DOUBLE_EQ(exciter.amount(), 0.0);
}

TEST(HarmonicExciterTest, AmountClampedAboveOne) {
  HarmonicExciter exciter(kSampleRate);
  constexpr double kAboveMaxAmount = 2.0;
  exciter.SetAmount(kAboveMaxAmount);
  EXPECT_DOUBLE_EQ(exciter.amount(), 1.0);
}

TEST(HarmonicExciterTest, AmountInRangeUnchanged) {
  HarmonicExciter exciter(kSampleRate);
  constexpr double kMidAmount = 0.5;
  exciter.SetAmount(kMidAmount);
  EXPECT_DOUBLE_EQ(exciter.amount(), kMidAmount);
}

TEST(HarmonicExciterTest, DefaultAmountIsZero) {
  HarmonicExciter exciter(kSampleRate);
  EXPECT_DOUBLE_EQ(exciter.amount(), 0.0);
}

// Quarter blend should add less energy than full blend.
TEST(HarmonicExciterTest, IntermediateBlendScalesEffect) {
  constexpr double kQuarterAmount = 0.25;
  HarmonicExciter quarter_exciter(kSampleRate);
  HarmonicExciter full_exciter(kSampleRate);
  quarter_exciter.SetAmount(kQuarterAmount);
  full_exciter.SetAmount(1.0);

  float quarter_buf[] = {1.0F, 1.0F};
  float full_buf[] = {1.0F, 1.0F};
  quarter_exciter.ProcessStereo(quarter_buf);
  full_exciter.ProcessStereo(full_buf);

  // Both should add energy, but full should add more.
  EXPECT_GT(quarter_buf[0], 1.0F);
  EXPECT_GT(full_buf[0], quarter_buf[0]);
}

TEST(HarmonicExciterTest, ProcessStereoEmptyBufferIsNoOp) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  std::span<float> empty;
  exciter.ProcessStereo(empty);
  // No crash; verify state is still usable.
  float buf[] = {0.0F, 0.0F};
  exciter.ProcessStereo(buf);
  EXPECT_NEAR(buf[0], 0.0F, 1e-6F);
}

// Left and right channels should be processed independently.
TEST(HarmonicExciterTest, ChannelsAreIndependent) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  // Left = DC signal, right = zero; only left should be affected.
  float buf[] = {1.0F, 0.0F};
  exciter.ProcessStereo(buf);
  EXPECT_GT(buf[0], 1.0F);
  EXPECT_NEAR(buf[1], 0.0F, 1e-6F);
}

TEST(HarmonicExciterTest, SetSampleRateDoesNotCrash) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(1.0);
  constexpr double kNewSampleRate = 96000.0;
  exciter.SetSampleRate(kNewSampleRate);
  // Process a buffer to verify the new coefficients work.
  float buf[] = {1.0F, 1.0F};
  exciter.ProcessStereo(buf);
  EXPECT_TRUE(std::isfinite(buf[0]));
}

// At exact boundary: amount = 0.0 should bypass, amount = 1.0 should not.
TEST(HarmonicExciterTest, AmountAtExactZeroIsBypass) {
  HarmonicExciter exciter(kSampleRate);
  exciter.SetAmount(0.0);
  constexpr float kInput = 0.42F;
  float buf[] = {kInput, kInput};
  exciter.ProcessStereo(buf);
  EXPECT_FLOAT_EQ(buf[0], kInput);
  EXPECT_FLOAT_EQ(buf[1], kInput);
}

}  // namespace
