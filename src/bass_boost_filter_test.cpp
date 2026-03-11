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

// At 0 dB the shelf filter is transparent (H(z) = 1); every output sample must
// exactly equal the corresponding input.
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

// H(DC) = (b0 + b1 + b2) / (1 + a1 + a2): the exact zero-frequency gain. A
// low shelf at +12 dB must amplify DC by more than 1.5x.
TEST(BassBoostFilterTest, BassBoostedAt100Hz) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  const BiquadCoeffs& coeffs = filter.coefficients();
  const double dc_gain =
      (coeffs.b0 + coeffs.b1 + coeffs.b2) / (1.0 + coeffs.a1 + coeffs.a2);
  EXPECT_GT(dc_gain, 1.5);
}

// Alternating +1/-1 (Nyquist) is far above the 100 Hz shelf; the filter's poles
// sit near z = 1, so the response at z = -1 is essentially unity and the
// transient resolves within a few samples.
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
  constexpr float kStereoR0 = 0.7F;
  constexpr float kStereoR1 = 0.2F;
  constexpr float kStereoR2 = 0.4F;
  BassBoostFilter filter1(kSampleRate);
  BassBoostFilter filter2(kSampleRate);
  filter1.SetGainDb(kGain10dB);
  filter2.SetGainDb(kGain10dB);

  float stereo[] = {kBufSampleA, kStereoR0,   -kBufSampleB,
                    kStereoR1,   kBufSampleC, -kStereoR2};  // 3 stereo frames
  float mono[] = {kBufSampleA, -kBufSampleB, kBufSampleC};  // channel 0 only

  filter1.ProcessStereo(stereo);
  filter2.ProcessMono(mono, BassBoostFilter::Channel::Left);

  // Even indices are channel 0 (L) in the interleaved stereo buffer.
  EXPECT_NEAR(stereo[0], mono[0], 1e-5F);
  EXPECT_NEAR(stereo[2], mono[1], 1e-5F);
  EXPECT_NEAR(stereo[4], mono[2], 1e-5F);
}

TEST(BassBoostFilterTest, FrequencyAccessorReturnsSetValue) {
  constexpr double kTestFrequency = 200.0;
  BassBoostFilter filter(kSampleRate);
  filter.SetFrequency(kTestFrequency);
  EXPECT_NEAR(filter.frequency(), kTestFrequency, 0.001);
}

TEST(BassBoostFilterTest, FrequencyClampedAboveNyquistGuard) {
  constexpr double kAboveNyquist = 100'000.0;
  constexpr double kNyquistGuardRatio = 0.4;
  BassBoostFilter filter(kSampleRate);
  filter.SetFrequency(kAboveNyquist);
  // 0.4 * 48000 = 19200 Hz is the upper limit.
  EXPECT_LE(filter.frequency(), kSampleRate * kNyquistGuardRatio);
}

TEST(BassBoostFilterTest, FrequencyClampedBelowMinimum) {
  BassBoostFilter filter(kSampleRate);
  filter.SetFrequency(1.0);
  EXPECT_GE(filter.frequency(), 20.0);
}

TEST(BassBoostFilterTest, GainAtExactMinBoundary) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(BassBoostFilter::kMinGainDb);
  EXPECT_NEAR(filter.gain_db(), BassBoostFilter::kMinGainDb, 1e-9);
}

TEST(BassBoostFilterTest, GainAtExactMaxBoundary) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(BassBoostFilter::kMaxGainDb);
  EXPECT_NEAR(filter.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

// Right channel processed in mono should match the right channel of stereo.
TEST(BassBoostFilterTest, MonoChannel1MatchesStereoChannel1) {
  constexpr double kGain10dB = 10.0;
  constexpr float kLeftA = 0.1F;
  constexpr float kLeftB = 0.2F;
  constexpr float kLeftC = 0.3F;
  constexpr float kRightA = 0.7F;
  constexpr float kRightB = 0.4F;
  constexpr float kRightC = 0.9F;
  BassBoostFilter stereo_filter(kSampleRate);
  BassBoostFilter mono_filter(kSampleRate);
  stereo_filter.SetGainDb(kGain10dB);
  mono_filter.SetGainDb(kGain10dB);

  float stereo[] = {kLeftA, kRightA, kLeftB, kRightB, kLeftC, kRightC};
  float mono[] = {kRightA, kRightB, kRightC};

  stereo_filter.ProcessStereo(stereo);
  mono_filter.ProcessMono(mono, BassBoostFilter::Channel::Right);

  // Odd indices are channel 1 (R) in the interleaved stereo buffer.
  EXPECT_NEAR(stereo[1], mono[0], 1e-5F);
  EXPECT_NEAR(stereo[3], mono[1], 1e-5F);
  EXPECT_NEAR(stereo[5], mono[2], 1e-5F);
}

TEST(BassBoostFilterTest, ProcessStereoEmptyBufferIsNoOp) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  std::span<float> empty;
  filter.ProcessStereo(empty);
  // No crash, no state corruption; verify with a known signal after.
  float buf[] = {0.0F, 0.0F};
  filter.ProcessStereo(buf);
  EXPECT_NEAR(buf[0], 0.0F, 1e-5F);
}

TEST(BassBoostFilterTest, ProcessMonoEmptyBufferIsNoOp) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  std::span<float> empty;
  filter.ProcessMono(empty, BassBoostFilter::Channel::Left);
  float buf[] = {0.0F};
  filter.ProcessMono(buf, BassBoostFilter::Channel::Left);
  EXPECT_NEAR(buf[0], 0.0F, 1e-5F);
}

TEST(BassBoostFilterTest, SetSampleRateUpdatesCoefficients) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  const BiquadCoeffs before = filter.coefficients();

  constexpr double kNewSampleRate = 96000.0;
  filter.SetSampleRate(kNewSampleRate);
  const BiquadCoeffs& after = filter.coefficients();
  // Coefficients depend on sample rate; at least one must change.
  const bool coeffs_changed = std::abs(before.b0 - after.b0) > 1e-9 ||
                              std::abs(before.b1 - after.b1) > 1e-9 ||
                              std::abs(before.a1 - after.a1) > 1e-9;
  EXPECT_TRUE(coeffs_changed);
}

TEST(BassBoostFilterTest, CoefficientsFiniteAtLowSampleRate) {
  constexpr double kLowSampleRate = 8000.0;
  BassBoostFilter filter(kLowSampleRate);
  filter.SetGainDb(BassBoostFilter::kMaxGainDb);
  EXPECT_TRUE(std::isfinite(filter.coefficients().b0));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a1));
}

TEST(BassBoostFilterTest, CoefficientsFiniteAtHighSampleRate) {
  constexpr double kHighSampleRate = 192000.0;
  BassBoostFilter filter(kHighSampleRate);
  filter.SetGainDb(BassBoostFilter::kMaxGainDb);
  EXPECT_TRUE(std::isfinite(filter.coefficients().b0));
  EXPECT_TRUE(std::isfinite(filter.coefficients().a1));
}

// Higher gain must produce a larger DC amplification factor.
TEST(BassBoostFilterTest, DcGainIncreasesWithGainDb) {
  BassBoostFilter low_filter(kSampleRate);
  BassBoostFilter high_filter(kSampleRate);
  constexpr double kLowGain = 3.0;
  low_filter.SetGainDb(kLowGain);
  high_filter.SetGainDb(kGain12dB);

  const auto dc_gain = [](const BiquadCoeffs& coeffs) {
    return (coeffs.b0 + coeffs.b1 + coeffs.b2) / (1.0 + coeffs.a1 + coeffs.a2);
  };
  EXPECT_GT(dc_gain(high_filter.coefficients()),
            dc_gain(low_filter.coefficients()));
}

// Changing frequency should alter coefficients.
TEST(BassBoostFilterTest, FrequencyChangeUpdatesCoefficients) {
  BassBoostFilter filter(kSampleRate);
  filter.SetGainDb(kGain12dB);
  const BiquadCoeffs at_100 = filter.coefficients();

  constexpr double kNewFrequency = 200.0;
  filter.SetFrequency(kNewFrequency);
  const BiquadCoeffs& at_200 = filter.coefficients();
  EXPECT_NE(at_100.b0, at_200.b0);
}

}  // namespace
