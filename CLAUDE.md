# Project - Claude instructions

## Baseline
- Follow the Google C++ Style Guide as the default source of truth
- Use C++20 with no compiler extensions.
- Keep `.clang-format` and `.clang-tidy` clean.
- Ensure the project builds and tests pass before finishing changes.

### Build, run, and test
- Use `README.md` as the source of truth for build, run, and test commands.

## Personal-project overrides (non-production)
- The overrides below are personal conventions over Google C++ style guide
  for this repository only.
- They are allowed here for solo-project workflow consistency.
- In production or team code, do not carry these overrides forward unless the
  team explicitly adopts them.
- If any override conflicts with team or organization standards, team or
  organization standards win.

## Overrides

### File naming
- Use `.hpp` for C++ headers and `.cpp` for C++ source files.
- Do not use `.h` or `.cc` in this project.

```text
Good: bass_boost_filter.hpp, audio_pipeline.cpp
Bad:  bass_boost_filter.h,   audio_pipeline.cc
```


### Namespaces
- Keep nested namespace openings adjacent with no blank line between them.

```cpp
// Bad: blank line between nested namespace openings.
namespace endpoint_audio_format {

namespace {
}
}

// Good.
namespace endpoint_audio_format {
namespace {
...
}  // namespace
}  // namespace endpoint_audio_format
```

### Comments
- Use backticks only for real identifiers (`Initialize`, `kMaxGainDb`).
- Do not use backticks for prose, formulas, or `#endif` trailing comments.
- Keep comments ASCII-only.
- Use correct spelling and punctuation; end every sentence with a period.
- Wrap comments close to 80 columns at word boundaries.
- For non-obvious literal arguments (`0`, `nullptr`), add inline parameter
  labels.
- Skip parameter labels for obvious standard-library calls (for example,
  `memset`, `memcpy`).
- For non-obvious logic that must be commented, include concrete failure modes
  or constraints specific to this codebase.
- For `// NOLINT(...)` comments, warning suppressions, and unavoidable magic
  constants, explain why inline.
- Write comments that age well. Prefer constraints and invariants over naming
  specific callers, threads, functions, or anything that may change.
- Explain domain-specific terms when first introduced. Assume the reader is an
  experienced C++ engineer without domain expertise in this project area.
- When referencing platform error codes, describe the observable consequence
  instead of citing the symbolic constant. The reader may not know what the
  code means.

```cpp
// Bad: reader must look up the error code.
// Calling `Start()` again would return AUDCLNT_E_NOT_STOPPED.

// Good: states the consequence directly.
// Calling `Start()` a second time would fail because the stream is
// already running.
```

```cpp
// Bad: identifier not marked.
// Requires Initialize() to succeed before calling this path.

// Bad: Comment may go stale in the future.
// We will use this code later.

// Good.
// Requires `Initialize()` to succeed before calling this path.

// Good: formula text is not an identifier.
// H(DC) = (b0 + b1 + b2) / (1 + a1 + a2).
```

```cpp
// Good: no backticks in #endif trailing comments.
#endif  // _WIN32
```

```cpp
// Bad: Unicode symbols in comments can cause encoding issues.
// Gain is ±12 dB and limiter turns on near ±0.7.

// Good: ASCII-only text.
// Gain is +/-12 dB and limiter turns on near +/-0.7.
```

```cpp
// Bad: unclear literal arguments.
hr = client->Initialize(mode, 0, 0, 0, format, nullptr);

// Good: label non-obvious literals.
hr = client->Initialize(mode, /*stream_flags=*/0, /*hns_buffer=*/0,
                        /*hns_periodicity=*/0, format,
                        /*session_guid=*/nullptr);
```

```cpp
// Good: do not add parameter labels to obvious standard library calls.
std::memset(buffer, 0, size);
std::memcpy(dst, src, bytes);
```

```cpp
// Bad: short wrapped comment with avoidable line break.
// At 0 dB the shelf filter is transparent (H(z) = 1); every output sample
// must
// exactly equal the corresponding input.

// Good: wraps near 80 columns at word boundaries.
// Even indices are the L channel; R mirrors L in this buffer so one channel
// is sufficient to verify unity gain.
```

