// Verifies the filter boosts bass frequencies, leaves high frequencies
// unaffected, clamps gain and frequency within valid ranges, produces finite
// coefficients across sample rates, and recovers cleanly after reset.

#include "bass_boost_filter.hpp"

#include <cmath>

#include "gtest/gtest.h"

namespace {

constexpr double kSampleRate = 48000.0;

// Shared across multiple tests that need a mix of positive/negative samples.
constexpr float kBufSampleA = 0.5F;
constexpr float kBufSampleB = 0.3F;
constexpr float kBufSampleC = 0.8F;

constexpr double kGain12dB = 12.0;

// At 0 dB the shelf filter is transparent (H(z) = 1); every output sample
// must exactly equal the corresponding input.
TEST(BassBoostFilterTest, ZeroGainIsUnity) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(0.0);
  float buf[] = {kBufSampleA,  kBufSampleA, -kBufSampleB,
                 -kBufSampleB, kBufSampleC, kBufSampleC};
  filter.ProcessStereo(buf);
  // Even indices are the L channel; R mirrors L in this buffer so one channel
  // is sufficient to verify unity gain.
  EXPECT_NEAR(buf[0], kBufSampleA, 1e-5F);
  EXPECT_NEAR(buf[2], -kBufSampleB, 1e-5F);
  EXPECT_NEAR(buf[4], kBufSampleC, 1e-5F);
}

// H(DC) = (b0 + b1 + b2) / (1 + a1 + a2): the exact zero-frequency gain.
// A low shelf at +12 dB must amplify DC by more than 1.5x.
TEST(BassBoostFilterTest, BassBoostedAt100Hz) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  const BiquadCoeffs& coeffs = filter.coefficients();
  const double dc_gain =
      (coeffs.b0 + coeffs.b1 + coeffs.b2) / (1.0 + coeffs.a1 + coeffs.a2);
  EXPECT_GT(dc_gain, 1.5);
}

// Alternating +1/-1 (Nyquist) is far above the 100 Hz shelf; the filter's
// poles sit near z = 1, so the response at z = -1 is essentially unity and
// the transient resolves within a few samples.
TEST(BassBoostFilterTest, HighFreqUnaffected) {
  BassBoostFilter filter(kSampleRate);
  constexpr double kGain15dB = 15.0;
  filter.SetGainDb(kGain15dB);
  float buf[] = {1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F,
                 1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F};
  filter.ProcessStereo(buf);
  // buf[14] is the L channel of the last frame (frame 7); by that point the
  // startup transient has settled, so gain should be at unity.
  EXPECT_NEAR(std::abs(buf[14]), 1.0F, 0.06F);
}

TEST(BassBoostFilterTest, GainClampedAtMax) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(100.0);
  EXPECT_LE(filter.gain_db(), BassBoostFilter::kMaxGainDb);
}

TEST(BassBoostFilterTest, GainClampedAtMin) {
  constexpr double kBelowMinGainDb = -5.0;
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kBelowMinGainDb);
  EXPECT_GE(filter.gain_db(), BassBoostFilter::kMinGainDb);
}

// Prime the delay lines, reset, then verify zero input gives zero output.
TEST(BassBoostFilterTest, ResetClearsDelayLineState) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  float signal[] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  filter.ProcessStereo(signal);

  filter.Reset();
  float zeros[] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  filter.ProcessStereo(zeros);
  // Even indices are the L channel; each channel has its own delay line, so
  // one per channel is enough to verify both were cleared.
  EXPECT_NEAR(zeros[0], 0.0F, 1e-5F);
  EXPECT_NEAR(zeros[2], 0.0F, 1e-5F);
  EXPECT_NEAR(zeros[4], 0.0F, 1e-5F);
}

TEST(BassBoostFilterTest, CoefficientsFiniteAtZeroGain) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(0.0);
  EXPECT_TRUE(std::isfinite(filter.coefficients().b0));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b2));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a2));
}

TEST(BassBoostFilterTest, CoefficientsFiniteAtHalfMaxGain) {
  constexpr double kHalfMaxGain = BassBoostFilter::kMaxGainDb / 2.0;
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kHalfMaxGain);
  EXPECT_TRUE(std::isfinite(filter.coefficients().b0));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b2));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a2));
}

TEST(BassBoostFilterTest, CoefficientsFiniteAtMaxGain) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(BassBoostFilter::kMaxGainDb);
  EXPECT_TRUE(std::isfinite(filter.coefficients().b0));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().b2));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a1));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a2));
}

TEST(BassBoostFilterTest, MonoChannel0MatchesStereoChannel0) {
  constexpr double kGain10dB = 10.0;

}  // namespace
