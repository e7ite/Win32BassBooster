# Win32 Bass Booster

[![Build][build-badge]][build-workflow]

A simple, system-wide bass boost application for Windows.

![Win32 Bass Booster screenshot](bass-booster-screenshot.png)

# How to use

If you just want to use the app, download the latest packaged build from
[Releases][github-releases], extract the archive, and run
`Win32BassBooster.exe`.

Then drag the slider to choose how much bass boost you want. The window footer
shows the current default render device name when startup succeeds.

# How to build

## Prerequisites

- **Windows 10 or later**
- **Visual Studio 2022** (Community, Professional, or Enterprise) with the
  **Desktop development with C++** workload, or the free
  [Build Tools for Visual Studio 2022][vs2022-build-tools] with the same
  workload selected
- **CMake 3.16+** -- included with Visual Studio 2022 and also available
  standalone from [cmake.org][cmake-download]

Optional but strongly recommended:

- **LLVM** with `clang-format` and `clang-tidy` on `PATH`

All commands below should be run from a **Developer Command Prompt for VS
2022**, or any terminal where `cmake` and the MSVC toolchain are already on
`PATH`.

## Building and testing

```bat
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The built executable is written to `build\bin\Release\Win32BassBooster.exe`.

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

Google Test and Google Mock are fetched automatically by CMake on first
configure.

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

- [C/C++][vscode-cpptools] (ms-vscode.cpptools) -- IntelliSense and debugging
- [CMake Tools][vscode-cmake-tools] (ms-vscode.cmake-tools) -- configure,
  build, and test without leaving the editor

### Open the project

Use **File -> Open Folder** and select the `Win32BassBooster` directory. CMake
Tools will detect `CMakePresets.json` and offer to configure automatically.

### Select a configure preset

The project uses **CMake presets** instead of kit selection. When prompted, or
via **Ctrl+Shift+P -> CMake: Select Configure Preset**, choose:

```text
Visual Studio 2022 x64
```

This selects the Visual Studio 2022 generator with x64 architecture and
enables both the GUI and test targets.

### Select a build preset

Open **Ctrl+Shift+P -> CMake: Select Build Preset** and choose **release** or
**debug**.

### Build

Press **F7**, click the **Build** button in the status bar, or run:

```text
Ctrl+Shift+P -> CMake: Build
```

The executable is written to `build\bin\Release\Win32BassBooster.exe` or
`build\bin\Debug\Win32BassBooster.exe`.

### Troubleshooting: platform mismatch

If you see an error like `"generator platform: x64 does not match the platform
used previously"`, delete the stale CMake cache and reconfigure:

```bat
rmdir /s /q build\CMakeCache.txt build\CMakeFiles
```

Then reopen the folder or run **Ctrl+Shift+P -> CMake: Configure**.

### Run the tests

Open the **Testing** panel (beaker icon in the Activity Bar) to run and
inspect individual tests, or run all tests at once:

```text
Ctrl+Shift+P -> CMake: Run Tests
```

### Run or debug the application

Use the status bar **launch target** selector to pick
**Win32BassBooster**, then:

- **Ctrl+Shift+P -> CMake: Run Without Debugging** -- launch the built exe
- **Ctrl+Shift+P -> CMake: Debug** -- launch under the debugger with
  breakpoints

## Project layout

```text
.
|-- src/
|   |-- bass_boost_filter.hpp/.cpp            # Biquad low-shelf bass boost
|   |-- harmonic_exciter.hpp/.cpp             # Standalone harmonic enhancer module
|   |-- endpoint_audio_format.hpp/.cpp        # Endpoint PCM decode to stereo float
|   |-- audio_pipeline_interface.hpp          # Abstract audio pipeline contract
|   |-- loopback_capture_activation.hpp/.cpp  # Process-loopback capture activation
|   |-- audio_pipeline.hpp/.cpp               # Startup, recovery, and bass-delta render path
|   |-- theme_manager.hpp/.cpp                # Dark/light palette + title bar theming
|   |-- main_window.hpp/.cpp                  # Themed Win32 window and slider UI
|   |-- main.cpp                              # Entry point
|   `-- *_test.cpp                            # Unit tests (one per module)
|-- resources/
|   `-- app.rc                                # Manifest and version info
|-- .github/
|   `-- workflows/build.yml                   # CI: build + test on every push/PR
|-- .githooks/
|   `-- pre-commit                            # Auto-formats staged files on commit
|-- .clang-format                             # Code style configuration
|-- .clang-tidy                               # Static analysis configuration
|-- bass-booster-architecture.svg             # README high-level system diagram
|-- bass-booster-screenshot.png               # README application screenshot
|-- CMakePresets.json                         # Configure/build/test presets
|-- LICENSE                                   # License terms for this repository
`-- CMakeLists.txt
```

## Code style

The project uses two LLVM tools to keep code clean:

| Tool | Role |
|---|---|
| `clang-format` | Auto-formats sources to Google C++ style (see `.clang-format` at project root) |
| `clang-tidy` | Static analysis at build time (see `.clang-tidy` at project root) |

[`CLAUDE.md`](CLAUDE.md) contains additional local code style and workflow overrides specific to this repository. Refer to it for repo-specific formatting and contribution rules.

Both tools are expected for normal contributions. CMake prints a warning during
configure if either tool is missing, and CI enforces them on push and pull
request builds.

### Installing LLVM on Windows

1. Go to the [LLVM releases page][llvm-releases] and download the Windows
   installer.
2. Run the installer and add LLVM to `PATH`.
3. Open a new terminal and verify:

   ```bat
   clang-format --version
   clang-tidy --version
   ```

4. Re-run CMake configure so it picks up the tools:

   ```bat
   cmake -B build
   ```

### Registering the pre-commit hook

`cmake -B build` registers `.githooks` as the local git hooks directory
automatically. After that, every `git commit` auto-formats staged `.cpp` and
`.hpp` files with `clang-format` before the commit is recorded.

If `clang-format` is not on `PATH`, the hook prints a warning and proceeds, so
you can still commit locally, but this may result in CI failures due to formatting issues, requiring additional commits to fix style problems.

### Formatting all sources manually

```bat
cmake --build build --target format
```

### Disabling clang-tidy locally

If you need to build without clang-tidy for a quick local iteration, pass:

```bat
cmake -B build -DENABLE_CLANG_TIDY=OFF
```

Do not land code built this way without running the normal checks again.

## CI and branch protection

GitHub Actions builds and tests every push and pull request. The badge above
reflects the current status of `main`.

To prevent merging broken code, enable branch protection in the GitHub
repository settings:

1. **Settings -> Branches -> Add branch protection rule** for `main`
2. Enable **Require status checks to pass before merging** and select the
   `build` check
3. Enable **Require branches to be up to date before merging**
4. Enable **Do not allow bypassing the above settings**

With these rules in place, no PR can be merged and no direct push to `main`
can succeed unless the CI build and all tests pass.

## How it works

In the simplest terms, it intercepts all audio playing through your default
output device, boosts the low frequencies, and plays it back.

![Win32 Bass Booster high-level system diagram](bass-booster-architecture.svg)

In more detail, the live pipeline captures audio headed to the default output
device, applies a low-shelf bass boost, subtracts the original full-band
signal, and renders only the added low-frequency delta back to that same
device. Rendering only the delta boosts bass without replaying a delayed copy
of the full signal.

### Capture and render setup

The render side opens the current default output endpoint in shared mode and
requires its mix format to be packed float32 stereo.

The capture side uses Windows process loopback through
`ActivateAudioInterfaceAsync`, wrapped in `loopback_capture_activation.cpp`, to
obtain a loopback `IAudioClient`. In plain terms, process loopback means
reading the audio headed to the default output device instead of recording from
a microphone.

This activation path also excludes this process tree from capture. That
prevents the rendered bass delta from being captured again and fed back into
the processing path.

That capture stream uses whatever format the render endpoint uses, so the
pipeline reuses the negotiated render mix format as the capture format too.

The capture and render loops run on a dedicated high-priority thread using
`AvSetMmThreadCharacteristicsW("Pro Audio", ...)`. If a stream call fails, the
audio thread tries to reacquire the current default render endpoint and reopen
both clients before giving up.

### Per-packet processing

Each captured packet goes through these steps:

1. `endpoint_audio_format` decodes the endpoint packet to interleaved stereo
   float samples.
2. `bass_boost_filter` applies a low-shelf biquad centered at 100 Hz.
3. The pipeline subtracts the original samples from the filtered samples and
   renders only the difference.

#### Low-shelf biquad filter (`bass_boost_filter.hpp/.cpp`)

A biquad is a second-order IIR (infinite impulse response) filter derived here
from the [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)
low-shelf formula.

The difference equation is:

```text
y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
               - a1*y[n-1] - a2*y[n-2]
```

Five coefficients (`b0`, `b1`, `b2`, `a1`, `a2`) describe the filter. Two
delay-line values per channel preserve history across audio buffers, so the
output remains continuous instead of clicking at packet boundaries.

All frequencies below the shelf frequency (100 Hz) are boosted by `gain_db`
dB. Frequencies above 100 Hz stay close to the original signal. The filter uses
a Butterworth Q of 0.707 for a maximally flat response.

#### Slider mapping

The slider position `p` in `[0, 1]` maps to gain via a square-root curve:

```cpp
gain_db = kMaxGainDb * sqrt(p);  // p=0 -> 0 dB, p=1 -> 18 dB
```

The square-root curve is convex, so the midpoint already produces about 70.7%
of the maximum gain. That makes the boost audible early in the slider travel.

#### Why render only the delta?

The low-shelf filter leaves mids and highs close to the original signal. By
rendering `filter(signal) - signal` instead of the full filtered output, the
app adds only the bass energy introduced by the shelf. That avoids the comb
filtering that would come from replaying a delayed full-band copy of the system
mix and removes the need for the older output attenuation stage.

#### Harmonic exciter status

`harmonic_exciter` is still part of the repository and has its own tests, but
the current live `audio_pipeline` does not inject it into the render path. The
active output path is the low-shelf bass delta described above.

### Thread safety

`SetBoostLevel` is called from the UI thread while the audio thread is running.
The user-controlled DSP parameters are stored in atomics, so the audio thread
can read them without taking locks on the hot path.

## License

See [LICENSE](LICENSE) for the license terms for this repository.

[build-badge]:
https://github.com/e7ite/Win32BassBooster/actions/workflows/build.yml/badge.svg
[build-workflow]:
https://github.com/e7ite/Win32BassBooster/actions/workflows/build.yml
[github-releases]: https://github.com/e7ite/Win32BassBooster/releases
[vs2022-build-tools]: https://aka.ms/vs/17/release/vs_BuildTools.exe
[cmake-download]: https://cmake.org/download/
[vscode-cpptools]:
https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools
[vscode-cmake-tools]:
https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools
[llvm-releases]: https://github.com/llvm/llvm-project/releases/latest
