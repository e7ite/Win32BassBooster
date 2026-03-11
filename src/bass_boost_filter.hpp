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
  double b0 = 1.0;
  double b1 = 0.0;
  double b2 = 0.0;
  double a1 = 0.0;
  double a2 = 0.0;
};

class BassBoostFilter {
 public:
  enum class Channel {
    Left = 0,
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

  explicit BassBoostFilter(double sample_rate = kDefaultSampleRate);

  // Sets gain in dB. Clamped to [`kMinGainDb`, `kMaxGainDb`]. Thread-safe:
  // may be called from any thread while the audio loop runs.
  void SetGainDb(double gain_db);

  // Sets the shelf cutoff in Hz. Clamped to [20 Hz, 0.4 x `sample_rate`] to
  // stay within the audible range and away from the Nyquist limit.
  void SetFrequency(double freq_hz);

  // Not thread-safe: call only when the audio loop is stopped, then `Reset()`.
  void SetSampleRate(double sample_rate);

  // Clears filter delay-line state; call after `SetSampleRate()` or on stream
  // restart to avoid a burst of stale samples at the output.
  void Reset();

  // Processes interleaved stereo in place: samples holds L,R pairs.
  void ProcessStereo(std::span<float> samples);

  // Processes a single channel from a buffer in place. `channel` selects which
  // delay-line state to use.
  void ProcessMono(std::span<float> samples, Channel channel);

  [[nodiscard]] double gain_db() const noexcept { return gain_db_.load(); }

  [[nodiscard]] double frequency() const noexcept { return freq_; }

  [[nodiscard]] const BiquadCoeffs& coefficients() const noexcept {
    return coeffs_;
  }

 private:
  double sample_rate_;
  double freq_ = kDefaultFreq;
  // atomic so gain can be updated from any thread while audio is processing.
  std::atomic<double> gain_db_ = 0.0;

  BiquadCoeffs coeffs_;
  // Per-channel IIR delay state for the Direct Form II transposed structure.
  std::array<double, kChannels> z1_ = {};
  std::array<double, kChannels> z2_ = {};
};

#endif  // WIN32BASSBOOSTER_SRC_BASS_BOOST_FILTER_HPP_
