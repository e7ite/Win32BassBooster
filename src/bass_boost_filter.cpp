// Low-shelf biquad EQ filter.

#include "bass_boost_filter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr double kMinFreqHz = 20.0;
// 0.4x keeps the cutoff at least one octave below Nyquist (half the sample
// rate), where filter coefficient math stays numerically stable.
constexpr double kNyquistGuardRatio = 0.4;

[[nodiscard]] double ClampGain(double gain_db) noexcept {
  return std::clamp(gain_db, BassBoostFilter::kMinGainDb,
                    BassBoostFilter::kMaxGainDb);
}

[[nodiscard]] constexpr size_t ToIndex(
    BassBoostFilter::Channel channel) noexcept {
  return static_cast<size_t>(channel);
}

// Inputs to the Audio EQ Cookbook low-shelf formula. Grouped so the three
// values that fully determine the shelf cannot be accidentally transposed.
struct ShelfParams {
  double gain_db;
  double freq_hz;
  double sample_rate;
};

[[nodiscard]] BiquadCoeffs ComputeCoeffs(ShelfParams params) {
  // Audio EQ Cookbook -- Low Shelf https://www.w3.org/TR/audio-eq-cookbook/
  // `a`: linear amplitude ratio for the shelf. /40 (not /20) because a low
  // shelf applies half the gain at its midpoint - the cookbook defines it this
  // way so the shelf gain at `w0` equals exactly `gain_db` dB.
  const double a =  // NOLINT(readability-identifier-length) - Cookbook symbol
      std::pow(10.0, params.gain_db / 40.0);
  // `w0`: angular frequency in radians, mapping the Hz cutoff to [0, 2*pi]
  // range that IIR filter math operates in.
  const double w0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      2.0 * std::numbers::pi * params.freq_hz / params.sample_rate;
  const double cos_w0 = std::cos(w0);
  // `alpha`: filter bandwidth factor derived from Q; higher Q = narrower
  // bandwidth = steeper rolloff at the shelf edge.
  const double alpha = std::sin(w0) / (2.0 * BassBoostFilter::kDefaultQ);
  const double two_sqrt_a_alpha = 2.0 * std::sqrt(a) * alpha;
  // `a0`: normalization denominator; dividing every coefficient by it keeps the
  // filter stable.
  const double a0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      (a + 1.0) + ((a - 1.0) * cos_w0) + two_sqrt_a_alpha;

  BiquadCoeffs coeffs;
  coeffs.b0 = (a * ((a + 1.0) - ((a - 1.0) * cos_w0) + two_sqrt_a_alpha)) / a0;
  coeffs.b1 =
      (2.0 * a *  // NOLINT(readability-magic-numbers) - Cookbook formula
       ((a - 1.0) - ((a + 1.0) * cos_w0))) /
      a0;
  coeffs.b2 = (a * ((a + 1.0) - ((a - 1.0) * cos_w0) - two_sqrt_a_alpha)) / a0;
  coeffs.a1 = (-2.0 *  // NOLINT(readability-magic-numbers) - Cookbook formula
               ((a - 1.0) + ((a + 1.0) * cos_w0))) /
              a0;
  coeffs.a2 = ((a + 1.0) + ((a - 1.0) * cos_w0) - two_sqrt_a_alpha) / a0;
  return coeffs;
}

[[nodiscard]] double ProcessSample(double input, const BiquadCoeffs& coeffs,
                                   double& delay1, double& delay2) noexcept {
  const double output = (coeffs.b0 * input) + delay1;
  delay1 = (coeffs.b1 * input) - (coeffs.a1 * output) + delay2;
  delay2 = (coeffs.b2 * input) - (coeffs.a2 * output);
  return output;
}

}  // namespace

BassBoostFilter::BassBoostFilter(double sample_rate)
    : sample_rate_(sample_rate) {
  coeffs_ = ComputeCoeffs({.gain_db = gain_db_.load(),
                           .freq_hz = freq_,
                           .sample_rate = sample_rate_});
}

void BassBoostFilter::SetGainDb(double gain_db) {
  gain_db_.store(ClampGain(gain_db));
  coeffs_ = ComputeCoeffs({.gain_db = gain_db_.load(),
                           .freq_hz = freq_,
                           .sample_rate = sample_rate_});
}

void BassBoostFilter::SetFrequency(double freq_hz) {
  freq_ = std::max(kMinFreqHz,
                   std::min(freq_hz, sample_rate_ * kNyquistGuardRatio));
  coeffs_ = ComputeCoeffs({.gain_db = gain_db_.load(),
                           .freq_hz = freq_,
                           .sample_rate = sample_rate_});
}

void BassBoostFilter::SetSampleRate(double sample_rate) {
  sample_rate_ = sample_rate;
  coeffs_ = ComputeCoeffs({.gain_db = gain_db_.load(),
                           .freq_hz = freq_,
                           .sample_rate = sample_rate_});
  Reset();
}

void BassBoostFilter::Reset() {
  z1_.fill(0.0);
  z2_.fill(0.0);
}

void BassBoostFilter::ProcessStereo(std::span<float> samples) {
  constexpr size_t kLeftIndex = ToIndex(Channel::Left);
  constexpr size_t kRightIndex = ToIndex(Channel::Right);
  for (size_t i = 0; i < samples.size(); i += kChannels) {
    // `ProcessSample` runs in double to prevent IIR rounding error from
    // accumulating across recursive taps; cast back to float at the buffer
    // boundary where the precision loss happens only once per sample and to
    // match WASAPI's 32-bit float format.
    samples[i] = static_cast<float>(
        ProcessSample(samples[i], coeffs_, z1_[kLeftIndex], z2_[kLeftIndex]));
    samples[i + 1] = static_cast<float>(ProcessSample(
        samples[i + 1], coeffs_, z1_[kRightIndex], z2_[kRightIndex]));
  }
}

void BassBoostFilter::ProcessMono(std::span<float> samples, Channel channel) {
  const size_t channel_index = ToIndex(channel);
  double& delay1 = z1_[channel_index];
  double& delay2 = z2_[channel_index];
  for (float& sample : samples) {
    // Same float<->double boundary as `ProcessStereo`.
    sample = static_cast<float>(ProcessSample(sample, coeffs_, delay1, delay2));
  }
}
