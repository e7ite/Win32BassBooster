# Win32 Bass Booster

[![Build][build-badge]][build-workflow]

A system-wide bass boost application for Windows. Intercepts all system audio
via WASAPI loopback, runs it through a DSP chain (low-shelf biquad filter +
harmonic exciter + tanh soft limiter), and re-renders the processed audio
through a second shared-mode client.

## How it works

### WASAPI loopback capture

Windows Audio Session API (WASAPI) lets you open the default render endpoint in
**loopback mode** (`AUDCLNT_STREAMFLAGS_LOOPBACK`). This gives you a read view
of
the audio mixer's final output — whatever is playing through your speakers or
headphones — as a stream of raw PCM frames. A second WASAPI client is opened in
normal shared-mode to **write back** the processed audio. The result is that all
system audio is transparently intercepted, boosted, and returned before it
reaches
your hardware.

The capture and render loops run on a dedicated high-priority thread
(`AvSetMmThreadCharacteristicsW("Pro Audio", ...)`).

### DSP chain (per audio buffer)

Each 20 ms buffer goes through three stages in order:

#### 1. Harmonic exciter (`harmonic_exciter.hpp/.cpp`)

Headphones physically cannot reproduce frequencies below ~40–60 Hz. The harmonic
exciter works around this with psychoacoustics: the human brain can *infer* a
missing fundamental from its harmonics. If you hear a 120 Hz tone and a 180 Hz
tone together, you perceive a 60 Hz bass note even if 60 Hz is absent.

Implementation:
1. **100 Hz low-pass filter (biquad)** — isolates the sub-bass signal.
2. **Full-wave rectification** (`abs()`) — this is an even nonlinearity.
   Applying it to
   a 60 Hz sine `sin(ωt)` produces `|sin(ωt)|`, which contains the 2nd harmonic
   (120 Hz) and higher even harmonics. Your headphones can reproduce 120 Hz.
3. **40 Hz high-pass filter (biquad)** — removes DC and near-DC artifacts
   introduced
   by rectification.
4. **Blend 25% back** into the stereo signal.

This adds harmonic content in the range headphones can reproduce, and the brain
reconstructs the implied fundamental.

#### 2. Low-shelf biquad filter (`bass_boost_filter.hpp/.cpp`)

A second-order IIR (infinite impulse response) filter derived from the
[Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) low-shelf formula.

**What a biquad is:** A 2nd-order recursive filter described by the difference
equation:

```
y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2]
                - a1·y[n-1] - a2·y[n-2]
```

Five coefficients (`b0, b1, b2, a1, a2`) fully describe the filter. The two
`y[n-k]` feedback terms are stored in a 2-element delay line (`z1, z2`) per
channel, updated every sample. This is O(1) per sample and extremely cheap.

**Low-shelf behaviour:** All frequencies below the shelf frequency (100 Hz) are
boosted by `gain_db` dB. Frequencies above 100 Hz are passed through unchanged.
The Q factor (0.707, Butterworth) controls how gradually the shelf rolls off at
the cutoff — Butterworth is maximally flat, meaning no ripple in either the
passband or stopband.

**Slider mapping:** The slider position `p ∈ [0, 1]` maps to gain via a
square-root curve:

```cpp
gain_db = kMaxGainDb * sqrt(1.0 - p);  // p=0 → 15 dB, p=1 → 0 dB
```

The square-root curve is convex: at the midpoint of slider travel you get
~70.7% of max gain rather than 50%, so bass boost is audible immediately when
you move the slider.

#### 3. tanh soft limiter

A high shelf gain can push sample amplitudes above ±1.0, causing hard digital
clipping (the "pop" you'd hear at max boost). `std::tanh(x)` is applied
per-sample after the DSP chain:

- Near 0, `tanh(x) ≈ x` — transparent at normal levels.
- Above ~±0.7, it compresses smoothly toward the ±1.0 asymptote instead of
  hard-clipping.
- No abrupt transition; no audible artifact.

### Thread safety

Gain updates (`SetSliderPosition`) are issued from the UI thread and read on the
audio thread. The gain parameter is stored in a `std::atomic<double>`, so no
mutex is needed on the hot path.

## Prerequisites

- **Windows 10 or later**
- **Visual Studio 2019 or 2022** (Community, Professional, or Enterprise) with
  the **Desktop development with C++** workload, **or** the free
   [Build Tools for Visual Studio 2019][vs2019-build-tools] or
   [Build Tools for Visual Studio 2022][vs2022-build-tools]
  with the same workload selected.
- **CMake 3.16+** — included with Visual Studio 2019 16.5+ and all VS 2022
   releases; also available standalone from [cmake.org][cmake-download].

All commands below must be run from a **Developer Command Prompt for VS 2019**
(or VS 2022, or any terminal where `cmake` and the MSVC toolchain are on
`PATH`). You can open one from the Start menu:
*Visual Studio 2019 → Developer Command Prompt*.

## Building and testing

```bat
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The built executable is at `build\bin\Release\Win32BassBooster.exe`.

### Debug build

```bat
cmake -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Debug output lands in `build\bin\Debug\Win32BassBooster.exe`.

### Running the tests only

```bat
ctest --test-dir build -C Release --output-on-failure
```

Tests cover the DSP core (`bass_boost_filter`, `harmonic_exciter`) and all
Win32 modules. Google Test is fetched automatically by CMake on first configure.

### Reconfiguring from scratch

If you switch toolchains or hit a stale cache, delete the build directory and
re-run configure:

```bat
rmdir /s /q build
cmake -B build
cmake --build build --config Release
```

## Building on GNU/Linux (or WSL on Windows)

On Linux (Ubuntu, Debian, etc.) or Windows Subsystem for Linux, only the
cross-platform DSP core and its tests build. The Win32 GUI and audio-engine
modules are automatically skipped.

### Prerequisites

```bash
sudo apt install build-essential cmake
```

### Build and test

```bash
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
```

The DSP test binaries land in `build/bin/`:

- `bass_boost_filter_test`
- `harmonic_exciter_test`

The Win32-specific tests (`audio_pipeline_test`, `theme_manager_test`,
