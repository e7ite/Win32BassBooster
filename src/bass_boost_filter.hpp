// Single-band low-shelf equalizer for bass boost.
// Gain updates are safe from any thread while audio is processing.

#ifndef WIN32BASSBOOSTER_SRC_BASS_BOOST_FILTER_HPP_
#define WIN32BASSBOOSTER_SRC_BASS_BOOST_FILTER_HPP_

#include <array>
#include <atomic>
#include <span>

// A biquad is a second-order IIR (recursive) filter. `b0`/`b1`/`b2` weight the
// current and two previous inputs (feedforward path); `a1`/`a2` weight the two
// previous outputs (feedback path). Defaults give a unity (passthrough)
// response. Plain struct so tests can inspect computed values without accessing
// private state.
struct BiquadCoeffs {
  // Gain on the current input sample.
  double b0 = 1.0;
  // Gain on the one-sample-old input.
  double b1 = 0.0;
  // Gain on the two-samples-old input.
  double b2 = 0.0;
  // Gain on the one-sample-old output.
  double a1 = 0.0;
  // Gain on the two-samples-old output.
  double a2 = 0.0;
};

// Single-band low-shelf filter that boosts bass while keeping independent
// delay-line state for the left and right channels.
class BassBoostFilter {
 public:
  enum class Channel {
    // Uses the left-channel delay-line state.
    Left = 0,
    // Uses the right-channel delay-line state.
    Right = 1,
  };

  // 0 dB is the minimum: the filter is always additive, never attenuating.
  static constexpr double kMinGainDb = 0.0;
  // 18 dB ceiling: enough for a heavy bass boost. Process loopback renders
  // only the delta (filter - original), so clipping risk is low.
  static constexpr double kMaxGainDb = 18.0;

  // 100 Hz shelf targets the bass band that headphones can reproduce.
  static constexpr double kDefaultFreq = 100.0;

  // 1/sqrt(2): Butterworth Q gives a maximally flat passband with no resonant
  // peak.
  static constexpr double kDefaultQ = 0.707;

  // Left and right: each channel needs its own filter delay-line state.
  static constexpr int kChannels = 2;

  // Most common Windows audio session sample rate.
  static constexpr double kDefaultSampleRate = 48000.0;

  // Creates a filter configured for `sample_rate` Hz audio.
  explicit BassBoostFilter(double sample_rate = kDefaultSampleRate);

  // Sets gain in dB. Clamped to [`kMinGainDb`, `kMaxGainDb`]. Thread-safe:
  // `gain_db` may be called from any thread while the audio loop runs.
  void SetGainDb(double gain_db);

  // Sets the shelf cutoff to `freq_hz`. Clamped to [20 Hz,
  // 0.4 x `sample_rate_`] to stay within the audible range and away from the
  // Nyquist limit.
  void SetFrequency(double freq_hz);

  // Sets the sample rate to `sample_rate`. Not thread-safe: call only when the
  // audio loop is stopped, then `Reset()`.
  void SetSampleRate(double sample_rate);

  // Clears filter delay-line state; call after `SetSampleRate()` or on stream
  // restart to avoid a burst of stale samples at the output.
  void Reset();

  // Processes interleaved stereo in place. `samples` must contain L,R pairs.
  void ProcessStereo(std::span<float> samples);

  // Processes one channel in place. `samples` holds that channel's scalar
  // samples, and `channel` selects which delay-line state to use.
  void ProcessMono(std::span<float> samples, Channel channel);

  [[nodiscard]] double gain_db() const noexcept { return gain_db_.load(); }

  [[nodiscard]] double frequency() const noexcept { return freq_; }

  [[nodiscard]] const BiquadCoeffs& coefficients() const noexcept {
    return coeffs_;
  }

 private:
  // Stored because biquad coefficients depend on sample rate and must be
  // recomputed when either rate or gain changes.
  double sample_rate_;
  // Current shelf cutoff in Hz; stored to recompute coefficients on gain
  // change without re-supplying the frequency.
  double freq_ = kDefaultFreq;
  // Atomic so gain can be updated from any thread while audio is processing.
  std::atomic<double> gain_db_ = 0.0;

  // Precomputed biquad coefficients; updated on gain, frequency, or sample
  // rate change so `ProcessStereo` applies them without per-sample work.
  BiquadCoeffs coeffs_;
  // Per-channel memory of the one-sample-ago intermediate value. Without this
  // the filter would treat every buffer as if silence came before it, causing
  // audible clicks at buffer boundaries.
  std::array<double, kChannels> z1_ = {};
  // Per-channel memory of the two-samples-ago intermediate value. Together
  // with z1_ this gives the filter enough history to produce a smooth,
  // continuous output across buffer boundaries.
  std::array<double, kChannels> z2_ = {};
};

#endif  // WIN32BASSBOOSTER_SRC_BASS_BOOST_FILTER_HPP_
