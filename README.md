# Win32 Bass Booster

[![Build][build-badge]][build-workflow]

A simple, system-wide bass boost application for Windows.

## How to use

Simply run `Win32BassBooster.exe`, then drag the slider to control how much bass
you want. I tried finding something this simple out there, and couldn't. The
title bar and colors follow your Windows dark/light theme setting.

# How to build

## Prerequisites

- **Windows 10 or later**
- **Visual Studio 2019 or 2022** (Community, Professional, or Enterprise) with
  the **Desktop development with C++** workload, **or** the free
   [Build Tools for Visual Studio 2019][vs2019-build-tools] or
   [Build Tools for Visual Studio 2022][vs2022-build-tools]
  with the same workload selected.
- **CMake 3.16+** -- included with Visual Studio 2019 16.5+ and all VS 2022
   releases; also available standalone from [cmake.org][cmake-download].

All commands below must be run from a **Developer Command Prompt for VS 2019**
(or VS 2022, or any terminal where `cmake` and the MSVC toolchain are on
`PATH`). You can open one from the Start menu:
*Visual Studio 2019 -> Developer Command Prompt*.

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

Tests cover the DSP core (`bass_boost_filter`, `harmonic_exciter`,
`endpoint_audio_format`) and all Win32 modules. Google Test and Google Mock are
fetched automatically by CMake on first configure.

### Reconfiguring from scratch

If you switch toolchains or hit a stale cache, delete the build directory and
re-run configure:

```bat
rmdir /s /q build
cmake -B build
cmake --build build --config Release
```

## Building with VS Code

### Extensions

Install these two extensions from the VS Code Marketplace:

- [C/C++][vscode-cpptools] (ms-vscode.cpptools) -- IntelliSense and debugging.
- [CMake Tools][vscode-cmake-tools]
  (ms-vscode.cmake-tools) -- configure, build, and test without leaving the
  editor.

### Open the project

**File -> Open Folder** and select the `Win32BassBooster` directory. CMake
Tools will detect `CMakePresets.json` and offer to configure automatically.

### Select a configure preset

The project uses **CMake presets** instead of kit selection. When prompted, or
via **Ctrl+Shift+P -> CMake: Select Configure Preset**, choose:

```
Visual Studio 2022 x64
```

This selects the VS 2022 generator with x64 architecture and enables both the
GUI and test targets.

### Select a build preset

Open **Ctrl+Shift+P -> CMake: Select Build Preset** and choose **release** or
**debug**.

### Build

Press **F7**, click the **Build** button in the status bar, or:

```
Ctrl+Shift+P -> CMake: Build
```

The executable is written to `build\bin\Release\Win32BassBooster.exe` (or
`build\bin\Debug\` for a Debug build).

### Troubleshooting: platform mismatch

If you see an error like `"generator platform: x64 does not match the platform
used previously"`, delete the stale CMake cache and reconfigure:

```bat
rmdir /s /q build\CMakeCache.txt build\CMakeFiles
```

Then reopen the folder or run **Ctrl+Shift+P -> CMake: Configure**.

### Run the tests

Open the **Testing** panel (beaker icon in the Activity Bar) to run and
inspect individual tests, or run all of them at once:

```
Ctrl+Shift+P -> CMake: Run Tests
```

### Run / debug the application

Use the status bar **launch target** selector (next to the play button) to
pick **Win32BassBooster**, then:

- **Ctrl+Shift+P -> CMake: Run Without Debugging** -- launch the built exe.
- **Ctrl+Shift+P -> CMake: Debug** -- launch under the debugger with
  breakpoints.

## Project layout

```
.
├── src/
│   ├── bass_boost_filter.hpp/.cpp      # Biquad low-shelf IIR filter
│   ├── harmonic_exciter.hpp/.cpp       # Psychoacoustic bass enhancement
│   ├── endpoint_audio_format.hpp/.cpp  # Endpoint frame decoding to stereo float
│   ├── audio_pipeline_interface.hpp    # Abstract audio pipeline contract
│   ├── audio_pipeline.hpp/.cpp         # WASAPI loopback capture + re-render
│   ├── theme_manager.hpp/.cpp          # Dark/light palette + DWM title bar
│   ├── main_window.hpp/.cpp            # Top-level Win32 window
│   ├── main.cpp                        # Entry point
│   └── *_test.cpp                      # Unit tests (one per module)
├── resources/
│   └── app.rc                          # Manifest, version info
├── .github/
│   └── workflows/build.yml             # CI: build + test on every push/PR
├── .clang-format                       # Code style (Google C++ style)
├── .clang-tidy                         # Static analysis rules
├── .githooks/
│   └── pre-commit                      # Auto-formats staged files on commit
├── CMakePresets.json                    # CMake configure/build presets
└── CMakeLists.txt
```

## Code style

The project uses two LLVM tools to keep code clean:

| Tool | Role |
|---|---|
| `clang-format` | Auto-formats sources to Google C++ style (`.clang-format`) |
| `clang-tidy` | Static analysis at build time (`.clang-tidy`) |

`CLAUDE.md` contains additional local overrides used for this personal project.
Treat those as repo-specific workflow preferences. For production or team code,
default to Google C++ style (or the team's standard) unless the team explicitly
adopts the same overrides.

Both are **required to contribute**. CMake prints a `WARNING` during configure
if either tool is missing, and CI enforces them on every push/PR.

### Installing LLVM on Windows

1. Go to the [LLVM releases page][llvm-releases]
   and download the Windows installer -- look for a file named
   `LLVM-<version>-win64.exe`.
2. Run the installer. On the **"Add LLVM to the system PATH"** screen, select
   **"Add LLVM to the system PATH for all users"** (or current user).
3. Open a **new** terminal (the old one won't see the updated PATH) and verify:

   ```bat
   clang-format --version
   clang-tidy   --version
   ```

   Both commands should print a version string. If they print `'clang-format'
   is not recognized`, the LLVM `bin\` directory is not on `PATH` -- add it
   manually (typically `C:\Program Files\LLVM\bin`):

   ```bat
   setx PATH "%PATH%;C:\Program Files\LLVM\bin"
   ```

   Open a new terminal again and re-verify.

4. Re-run CMake configure so it picks up the tools:

   ```bat
   cmake -B build
   ```

   You should now see `-- clang-format: ...` and `-- clang-tidy: ...` in the
   configure output instead of warnings.

### Registering the pre-commit hook

`cmake -B build` registers `.githooks` as the git hooks directory
automatically. After that, every `git commit` auto-formats staged `.cpp`/`.hpp`
files with `clang-format` before the commit is recorded. If `clang-format` is
not on `PATH` the hook prints a warning and proceeds (so you can still commit
without it installed locally -- CI will catch any formatting issues).

### Formatting all sources manually

```bat
cmake --build build --target format
```

### Disabling clang-tidy locally

If you need to build without clang-tidy (e.g., for a quick local test), pass:

```bat
cmake -B build -DENABLE_CLANG_TIDY=OFF
```

This suppresses the warning and skips analysis. Do not land code built this way
-- the CI job always runs with clang-tidy enabled.

## CI and branch protection

GitHub Actions builds and tests every push and pull request. The badge above
reflects the current status of `main`.

To prevent merging broken code, enable branch protection in the GitHub
repository settings:

1. **Settings -> Branches -> Add branch protection rule** for `main`.
2. Enable **Require status checks to pass before merging** and select the
   `build` check.
3. Enable **Require branches to be up to date before merging**.
4. Enable **Do not allow bypassing the above settings**.

With these rules in place, no PR can be merged and no direct push to `main`
can succeed unless the CI build and all tests pass.

## How it works

In the simplest terms, it intercepts all audio playing through your default
output device, boosts the low frequencies, and plays it back.

### WASAPI loopback capture

WASAPI lets you open the default render endpoint in **loopback mode**
(`AUDCLNT_STREAMFLAGS_LOOPBACK`). This gives you a read view of the audio
mixer's final output as a stream of raw PCM frames. A second WASAPI client is
opened in normal shared-mode to **write back** the processed audio. The result
is that all system audio is transparently intercepted, boosted, and returned
before it reaches your hardware.

The capture and render loops run on a dedicated high-priority thread
(`AvSetMmThreadCharacteristicsW("Pro Audio", ...)`).

### DSP chain (per audio buffer)

Each buffer goes through three stages in order:

#### 1. Harmonic exciter (`harmonic_exciter.hpp/.cpp`)

Headphones physically cannot reproduce frequencies below ~40-60 Hz. The harmonic
exciter works around this with psychoacoustics: the human brain can *infer* a
missing fundamental from its harmonics. If you hear a 120 Hz tone and a 180 Hz
tone together, you perceive a 60 Hz bass note even if 60 Hz is absent.

Implementation:
1. **100 Hz low-pass filter (biquad)** -- isolates the sub-bass signal.
2. **Full-wave rectification** (`abs()`) -- this is an even nonlinearity.
   Applying it to a 60 Hz sine `sin(wt)` produces `|sin(wt)|`, which contains
   the 2nd harmonic (120 Hz) and higher even harmonics. Your headphones can
   reproduce 120 Hz.
3. **40 Hz high-pass filter (biquad)** -- removes DC and near-DC artifacts
   introduced by rectification.
4. **Blend 25% back** into the stereo signal.

This adds harmonic content in the range headphones can reproduce, and the brain
reconstructs the implied fundamental.

#### 2. Low-shelf biquad filter (`bass_boost_filter.hpp/.cpp`)

A second-order IIR (infinite impulse response) filter derived from the
[Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) low-shelf formula.

**What a biquad is:** A 2nd-order recursive filter described by the difference
equation:

```
y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
               - a1*y[n-1] - a2*y[n-2]
```

Five coefficients (`b0, b1, b2, a1, a2`) fully describe the filter. The two
`y[n-k]` feedback terms are stored in a 2-element delay line (`z1, z2`) per
channel, updated every sample. This is O(1) per sample and extremely cheap.

**Low-shelf behaviour:** All frequencies below the shelf frequency (100 Hz) are
boosted by `gain_db` dB. Frequencies above 100 Hz are passed through unchanged.
The Q factor (0.707, Butterworth) controls how gradually the shelf rolls off at
the cutoff -- Butterworth is maximally flat, meaning no ripple in either the
passband or stopband.

**Slider mapping:** The slider position `p` in [0, 1] maps to gain via a
square-root curve:

```cpp
gain_db = kMaxGainDb * sqrt(p);  // p=0 -> 0 dB, p=1 -> 15 dB
```

The square-root curve is convex: at the midpoint of slider travel you get
~70.7% of max gain rather than 50%, so bass boost is audible immediately when
you move the slider.

#### 3. tanh soft limiter

A high shelf gain can push sample amplitudes above +/-1.0, causing hard digital
clipping (the "pop" you'd hear at max boost). `std::tanh(x)` is applied
per-sample after the DSP chain:

- Near 0, `tanh(x) ~= x` -- transparent at normal levels.
- Above ~+/-0.7, it compresses smoothly toward the +/-1.0 asymptote instead of
  hard-clipping.
- No abrupt transition; no audible artifact.

### Thread safety

Gain updates (`SetBoostLevel`) are issued from the UI thread and read on the
audio thread. The gain parameter is stored in a `std::atomic<double>`, so no
mutex is needed on the hot path.

[build-badge]:
https://github.com/e7ite/Win32BassBooster/actions/workflows/build.yml/badge.svg
[build-workflow]:
https://github.com/e7ite/Win32BassBooster/actions/workflows/build.yml
[vs2019-build-tools]: https://aka.ms/vs/16/release/vs_BuildTools.exe
[vs2022-build-tools]: https://aka.ms/vs/17/release/vs_BuildTools.exe
[cmake-download]: https://cmake.org/download/
[vscode-cpptools]:
https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools
[vscode-cmake-tools]:
https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools
[llvm-releases]: https://github.com/llvm/llvm-project/releases/latest
