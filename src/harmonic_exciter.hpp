// Generates upper harmonics to improve perceived bass on small speakers.
// Blend updates are safe from any thread while audio is processing.

#ifndef WIN32BASSBOOSTER_SRC_HARMONIC_EXCITER_HPP_
#define WIN32BASSBOOSTER_SRC_HARMONIC_EXCITER_HPP_

#include <array>
#include <atomic>
#include <span>

class HarmonicExciter {
 public:
  // 100 Hz focuses on sub-bass fundamentals for deeper harmonic perception.
  static constexpr double kLpfFreq = 100.0;

  // Must be well below 2x `kLpfFreq` so the 2nd harmonic passes freely.
  static constexpr double kHpfFreq = 40.0;

  // 1/sqrt(2): Butterworth Q gives a maximally flat passband with no resonant
  // peak.
  static constexpr double kDefaultQ = 0.707;

  // 0.5: strong enough to make bass audible on small speakers/headphones that
  // roll off below 60 Hz; the tanh limiter prevents clipping.
  static constexpr double kMaxHarmonicGain = 0.5;

  // Left and right: each channel needs its own filter delay-line state.
  static constexpr int kChannels = 2;

  // Most common Windows audio session sample rate.
  static constexpr double kDefaultSampleRate = 48000.0;

  explicit HarmonicExciter(double sample_rate = kDefaultSampleRate);

  // Sets the harmonic blend: 0.0 = bypass, 1.0 = full effect. Clamped to [0,
  // 1]. Thread-safe: may be called from any thread while audio is running.
  void SetAmount(double amount);

  // Not thread-safe: call only when the audio loop is stopped, then `Reset()`.
  void SetSampleRate(double sample_rate);

  // Clears filter delay-line state; call after `SetSampleRate()` or on stream
  // restart.
  void Reset();

  // Processes interleaved stereo in place: samples holds L,R pairs.
  void ProcessStereo(std::span<float> samples);

  // Returns the current blend amount in [0, 1].
  [[nodiscard]] double amount() const noexcept { return blend_amount_.load(); }

 private:
  // A biquad is a second-order IIR (recursive) filter. `b0`/`b1`/`b2` weight
  // the current and two previous inputs (feedforward path); `a1`/`a2` weight
  // the two previous outputs (feedback path). Defaults give a unity
  // (passthrough) response.
  struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
  };

  // IIR filters are recursive: each output depends on past outputs, so state
  // must persist between `ProcessStereo()` calls. `z1` and `z2` hold the filter
  // memory one and two samples behind the current sample.
  struct DelayLine {
    double z1 = 0.0;
    double z2 = 0.0;
  };

  // Returns Audio EQ Cookbook low-pass biquad coefficients for `kLpfFreq` at
  // the given sample rate.
  [[nodiscard]] static Biquad ComputeLpfCoeffs(double sample_rate);

  // Returns Audio EQ Cookbook high-pass biquad coefficients for `kHpfFreq` at
  // the given sample rate.
  [[nodiscard]] static Biquad ComputeHpfCoeffs(double sample_rate);

  // Advances one biquad section by one sample (Direct Form II transposed).
  static double ApplyBiquad(double input, const Biquad& coeffs,
                            DelayLine& delay) noexcept;

  // `std::atomic` is lock-free, so the audio loop can read the blend amount
  // without ever waiting - required because the audio loop has a hard real-time
  // deadline; missing it causes audio dropouts.
  std::atomic<double> blend_amount_ = 0.0;

  // Isolates the bass fundamental so only low frequencies feed the harmonic
  // generator.
  Biquad lpf_;
  // Removes sub-bass rumble so only the target frequency band reaches the
  // harmonic generator.
  Biquad hpf_;

  // Per-channel memory for the low-pass filter so output stays continuous
  // across buffer boundaries.
  std::array<DelayLine, kChannels> lp_dl_;
  // Per-channel memory for the high-pass filter so output stays continuous
  // across buffer boundaries.
  std::array<DelayLine, kChannels> hp_dl_;
};

#endif  // WIN32BASSBOOSTER_SRC_HARMONIC_EXCITER_HPP_
