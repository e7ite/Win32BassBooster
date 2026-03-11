// Harmonic exciter DSP chain.

#include "harmonic_exciter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

HarmonicExciter::HarmonicExciter(double sample_rate)
    : lpf_(ComputeLpfCoeffs(sample_rate)),
      hpf_(ComputeHpfCoeffs(sample_rate)) {}

void HarmonicExciter::SetAmount(double amount) {
  blend_amount_.store(std::clamp(amount, 0.0, 1.0));
}

void HarmonicExciter::SetSampleRate(double sample_rate) {
  lpf_ = ComputeLpfCoeffs(sample_rate);
  hpf_ = ComputeHpfCoeffs(sample_rate);
  Reset();
}

void HarmonicExciter::Reset() {
  for (auto& delay_line : lp_dl_) {
    delay_line = {};
  }
  for (auto& delay_line : hp_dl_) {
    delay_line = {};
  }
}

void HarmonicExciter::ProcessStereo(std::span<float> samples) {
  const double amount = blend_amount_.load();
  if (amount <= 0.0) {
    return;
  }
  const double gain = amount * kMaxHarmonicGain;

  // Per-sample exciter chain: the LPF isolates the bass fundamental, then
  // full-wave rectification (`std::abs`) folds negative half-cycles upward.
  // That nonlinear fold produces only even harmonics (2nd, 4th, ...) - the warm
  // octave-up character. The HPF then strips the rectified fundamental so only
  // the new harmonics remain, which are blended into the dry signal.
  for (size_t i = 0; i < samples.size(); i += kChannels) {
    // Interleaved stereo frame: `samples[i]` is left and `samples[i + 1]` is
    // right.
    for (int channel = 0; channel < kChannels; ++channel) {
      const size_t channel_index = static_cast<size_t>(channel);
      // `channel_index` selects both the interleaved lane and the matching
      // `lp_dl_` / `hp_dl_` state so each channel keeps independent IIR memory.
      // Upcast to double so IIR rounding error doesn't accumulate across
      // recursive taps; cast back to float at the buffer boundary.
      const double sample = samples[i + channel_index];
      const double bass = ApplyBiquad(sample, lpf_, lp_dl_[channel_index]);
      const double rectified = std::abs(bass);
      const double harmonics =
          ApplyBiquad(rectified, hpf_, hp_dl_[channel_index]);
      samples[i + channel_index] =
          static_cast<float>(sample + (gain * harmonics));
    }
  }
}

double HarmonicExciter::ApplyBiquad(double input, const Biquad& coeffs,
                                    DelayLine& delay) noexcept {
  const double output = (coeffs.b0 * input) + delay.z1;
  delay.z1 = (coeffs.b1 * input) - (coeffs.a1 * output) + delay.z2;
  delay.z2 = (coeffs.b2 * input) - (coeffs.a2 * output);
  return output;
}

HarmonicExciter::Biquad HarmonicExciter::ComputeLpfCoeffs(double sample_rate) {
  // `w0`: angular frequency in radians, mapping the Hz cutoff to [0, 2*pi]
  // range that IIR filter math operates in.
  const double w0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      2.0 * std::numbers::pi * kLpfFreq / sample_rate;
  const double cos_w = std::cos(w0);
  const double sin_w = std::sin(w0);
  // `alpha`: filter bandwidth factor derived from Q; higher Q = narrower
  // bandwidth = steeper rolloff.
  const double alpha = sin_w / (2.0 * kDefaultQ);
  // `a0`: normalization denominator; dividing every coefficient by it keeps the
  // filter stable.
  const double a0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      1.0 + alpha;
  Biquad coeffs;
  coeffs.b0 =
      (1.0 - cos_w) /
      (2.0 * a0);  // NOLINT(readability-magic-numbers) - Cookbook formula
  coeffs.b1 = (1.0 - cos_w) / a0;
  coeffs.b2 =
      (1.0 - cos_w) /
      (2.0 * a0);  // NOLINT(readability-magic-numbers) - Cookbook formula
  coeffs.a1 =
      (-2.0 * cos_w) /  // NOLINT(readability-magic-numbers) - Cookbook formula
      a0;

  coeffs.a2 = (1.0 - alpha) / a0;
  return coeffs;
}

HarmonicExciter::Biquad HarmonicExciter::ComputeHpfCoeffs(double sample_rate) {
  // Same variable meanings as `ComputeLpfCoeffs()`; only `kHpfFreq` differs.
  const double w0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      2.0 * std::numbers::pi * kHpfFreq /
      sample_rate;  // NOLINT(readability-magic-numbers) - Cookbook formula
  const double cos_w = std::cos(w0);
  const double sin_w = std::sin(w0);
  const double alpha = sin_w / (2.0 * kDefaultQ);
  const double a0 =  // NOLINT(readability-identifier-length) - Cookbook symbol
      1.0 + alpha;
  Biquad coeffs;
  coeffs.b0 =
      (1.0 + cos_w) /
      (2.0 * a0);  // NOLINT(readability-magic-numbers) - Cookbook formula
  coeffs.b1 = (-(1.0 + cos_w)) / a0;
  coeffs.b2 =
      (1.0 + cos_w) /
      (2.0 * a0);  // NOLINT(readability-magic-numbers) - Cookbook formula
  coeffs.a1 =
      (-2.0 * cos_w) /  // NOLINT(readability-magic-numbers) - Cookbook formula
      a0;
  coeffs.a2 = (1.0 - alpha) / a0;
  return coeffs;
}
