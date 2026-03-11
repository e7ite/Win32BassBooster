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

### Control flow and complexity
- Keep functions at or under 50 lines when practical.
- Prefer one nesting level. Add a second level only when there is no simpler
  correct form.
- Prefer guard clauses and early returns to flatten control flow.
- Separate logical units inside a function with a blank line so setup,
  validation, mutation, and handoff steps are easy to scan quickly.
- If a change would only be possible by adding deeper nesting, stop and report
  the constraint.
- Prefer simple constructs first (`if`, loops, anonymous-namespace helpers,
  direct function calls).
- Add complexity only when it is clearly justified by verified performance
  constraints or maintainability and scalability needs.
- Do not hide side effects in conditions (for example, assignment in `if`).
- Use the simplest dispatch construct that fits current complexity:
  direct conditionals or loops -> one-time switch or branch ->
  strategy or polymorphism.
- Escalate dispatch complexity only when simpler control flow cannot keep the
  code clear or maintainable.
- Avoid callback forwarding, non-generic template indirection, and recursion
  when direct control flow expresses the same behavior.
- If multiple code paths perform the same high-level action (`decode`,
  `detect`, `serialize`) but differ in implementation, choose dispatch style by
  complexity and expected growth.
- Prefer one switch or branch point when the number of strategies is small and
  each strategy body is short.
- Do not introduce class-based runtime polymorphism for a small fixed set of
  stateless strategies in a single call path (for example, 3-5 formats with
  short bodies).
- Prefer a strategy or polymorphic design when there are many strategies, each
  strategy has substantial logic, or the strategy set is expected to grow.
- In hot paths, account for runtime polymorphism cost. Virtual dispatch adds
  indirection and often an indirect branch that can hurt branch prediction and
  instruction-cache locality.
- Before adding polymorphism, check whether one-time dispatch outside the hot
  loop already removes repeated branching. If yes, keep the simpler design.
- In hot paths and repeated loops, compute invariant metadata once at the
  narrowest stable scope and reuse it in inner loops.
- Avoid repeating the same switch in multiple places. Centralize dispatch once
  and keep each strategy implementation in one location.

```cpp
// Bad: side effect hidden in condition.
if (FAILED(start_status = StartWorker()) && !TryRecover(start_status)) {
  return;
}

// Good: compute first, then branch.
start_status = StartWorker();
if (FAILED(start_status) && !TryRecover(start_status)) {
  return;
}
```

```cpp
// Bad: unrelated steps are crammed together.
const Status init = Init();
if (!init.ok()) {
  return init;
}
ApplySettings();
StartWorker();

// Good: blank lines separate logical units.
const Status init = Init();
if (!init.ok()) {
  return init;
}

ApplySettings();

StartWorker();
```

```cpp
// Bad: deep nesting.
void Process(const Frame& frame) {
  if (!frame.empty()) {
    if (is_running_) {
      for (float sample : frame) {
        if (sample > 0.0F) {
          Apply(sample);
        }
      }
    }
  }
}

// Good: guard clauses flatten control flow.
void Process(const Frame& frame) {
  if (frame.empty() || !is_running_) {
    return;
  }
  for (float sample : frame) {
    if (sample <= 0.0F) {
      continue;
    }
    Apply(sample);
  }
}
```

```cpp
// Good: simple, one-time dispatch for a small fixed set.
switch (mode) {
  case Mode::kPcm16:
    return DecodePcm16(bytes);
  case Mode::kFloat32:
    return DecodeFloat32(bytes);
}

// Bad: callback forwarding for local control flow.
RunStep([this](Status status) { return Recover(status); });

// Good: direct control flow.
const StepStatus step_status = RunStep();
if (FAILED(step_status.recover_status)) {
  return;
}

// Bad: template indirection for non-generic logic.
template <typename Fn>
void Execute(Fn fn) {
  fn();
}

// Good: concrete helper.
void ExecuteStart();
```

```cpp
// Bad: repeats invariant checks for every sample.
for (size_t index = 0; index < sample_count; ++index) {
  if (format.bits_per_sample == 16) {
    ...
  }
  if (format.channel_count == 2) {
    ...
  }
}

// Good: derive once, reuse in the loop.
const bool is_int16 = format.bits_per_sample == 16;
const bool is_stereo = format.channel_count == 2;
for (size_t index = 0; index < sample_count; ++index) {
  if (is_int16) {
    ...
  }
  if (is_stereo) {
    ...
  }
}
```

```cpp
// Bad: repeated switch across multiple call sites.
Value HandleLeft(...) {
  switch (mode) {
    ...
  }
}
Value HandleRight(...) {
  switch (mode) {
    ...
  }
}

// Good: switch once, then reuse selected behavior.
HandlerFunction handle = SelectHandlerFunction(mode);
left = handle(left_input);
right = handle(right_input);
```

### Refactoring behavior
- Remove dead state, stale flags, and obsolete branches during the same
  refactor.
- Minimize scope at every level. Keep logic in the narrowest scope that works:
  local variable -> anonymous namespace -> `private` member ->
  `public` member or free function in a header. Promote scope only when
  something genuinely needs to be shared.
- When splitting long functions, prefer anonymous-namespace functions with
  explicit inputs and outputs before adding new member functions.
- Keep extracted helpers explicit: prefer clear inputs and outputs over hidden
  shared state.

```cpp
// Bad: `RefreshCoefficients` is public but only `SetLevelDb` calls it.
class ToneFilter {
 public:
  void SetLevelDb(double gain_db);
  void RefreshCoefficients();  // internal detail leaked into the interface.
};

// Good: free helper in an anonymous namespace with explicit inputs.
namespace {
FilterCoefficients ComputeSectionCoefficients(double gain_db,
                                              double sample_rate) {
  ...
}
}  // namespace
```

```cpp
// Bad: dead state left after refactor.
bool cached_ready = false;  // no longer read anywhere.

// Good: dead state removed in the same change.
```

```cpp
// Bad: callback plumbing for local linear logic.
RunStep([this](HRESULT hr) {
  if (FAILED(hr)) {
    Recover(hr);
  }
});

// Good: direct control flow.
const HRESULT step = RunStep();
if (FAILED(step)) {
  Recover(step);
}
```

```cpp
// Good: split long member function with an anonymous-namespace helper.
namespace {
void StopClientsAndFinalizeTask(IAudioClient* capture_client,
                                IAudioClient* render_client,
                                std::atomic<bool>* running,
                                HANDLE task) {
  ...
}
}  // namespace
```

