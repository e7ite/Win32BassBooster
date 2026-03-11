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
- Fill every comment line as close to column 80 as possible, breaking only at
  word boundaries. Do not leave a line short when the next word would still
  fit within 80 columns.
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

### Naming
- Names should be clear (unambiguous about what it refers to) and precise
  (unambiguous about what it does not refer to). Longer is usually better than
  shorter, but stop adding words once those goals are met.
- Do not use generic module or library names like `utils`, `helpers`, or
  `common`. Name modules by their concrete responsibility.
- Prefer specific names over vague filler words such as `result`, `value`,
  `data`, `status`, `helper`, `utils`, or `manager`.
- Do not use abbreviations like `I` or `iface` in interface-related names.
- Prefer removing redundant type words entirely when surrounding context already
  makes the type clear. `processor` is better than `processor_interface`.
- If a use site starts sounding repetitive (`capture.capture_audio_client`),
  rename one side so the repeated concept appears only once. Prefer removing
  redundant words over introducing abbreviations.
- Avoid names in the same scope that differ only in word order. This applies
  to class members, struct fields, local variables, and function parameters
  alike. Names that read as rearrangements of the same words are easy to
  confuse and hard to distinguish at a glance. Rename so each has a distinct
  keyword.

```cpp
// Bad: near-anagrams in the same class; easy to swap by accident.
ComPtr<IAudioClient> capture_audio_client_;
ComPtr<IAudioCaptureClient> audio_capture_client_;

// Good: distinct suffixes make the difference immediately visible.
ComPtr<IAudioClient> capture_client_;
ComPtr<IAudioCaptureClient> capture_service_;

// Bad: local variables that are word-order rearrangements.
auto render_buffer_size = GetBufferSize();
auto buffer_render_size = GetOutputSize();

// Good: each name has a unique keyword.
auto render_buffer_size = GetBufferSize();
auto output_frame_count = GetOutputSize();
```

- For test doubles, do not encode the double type in variable names when the
  type already makes that clear. Name by role.
- A type name must cover all of its members, not just a subset. If the name
  only describes one member and the rest need "and" to explain, the name is
  too narrow.

```cpp
// Bad: struct holds a device, an enumerator, a name, and a status,
// but the name only describes the device.
struct DefaultEndpoint {
  Status status;
  ScopedComPtr<IMMDeviceEnumerator> enumerator;
  ScopedComPtr<IMMDevice> render_device;
  std::wstring endpoint_name;
};

// Good: name covers the whole bundle.
struct EndpointAcquisition {
  Status status;
  ScopedComPtr<IMMDeviceEnumerator> enumerator;
  ScopedComPtr<IMMDevice> render_device;
  std::wstring endpoint_name;
};
```

- Omit words clear from surrounding context:

```cpp
// Bad.
class IAudioSink {
};
std::unique_ptr<ProcessorInterface> processor_interface;
std::unique_ptr<ProcessorInterface> processor_iface;
HRESULT result = S_OK;

// Good.
class AudioSink {
};
std::unique_ptr<Processor> processor;
HRESULT render_start = S_OK;
CaptureSetupState& setup = ...;
setup.service.reset();

// Bad: variable name repeats the member concept; mock prefix is redundant
// with the type.
MockAudioEngine mock_audio_engine;
CaptureSetupState& capture = ...;
capture.capture_service.reset();

// Good.
MockAudioEngine audio_engine;
```

### Local variables
- Introduce a local variable only when its name adds clarity, it factors out a
  repeated expression, or it must live across statements.
- Avoid single-use temporary variables whose names add no information.

```cpp
// Bad: generic single-use variable.
const uint32_t value = capacity - used;
if (value < requested) {
  return;
}

// Good: name carries meaning.
const uint32_t available = capacity - used;
if (available < requested) {
  return;
}
```

### Lifetime and value storage
- Never store a `const T&` member from a constructor parameter. It can bind to
  temporaries and leave a dangling reference.
- Prefer the simplest storage that satisfies lifetime and ownership:
  bare object -> `std::optional<T>` when absence is real ->
  `std::unique_ptr<T>` when indirection or ownership transfer is required.
- Prefer smart pointers over owning raw pointers for heap objects. Ownership
  and ownership transfer should be visible in the type system so callers and
  reviewers can verify them by local inspection.
- Default to `std::make_unique<T>(...)` for heap allocation. Avoid
  `std::unique_ptr<T>(new T(...))` and `ptr.reset(new T(...))` when
  `std::make_unique` expresses the same intent.
- When legacy code returns ownership via `T*`, wrap it in a smart pointer
  immediately. When legacy code accepts ownership via `T*`, keep ownership in
  a smart pointer until that boundary.
- If ownership is moving between project types that already use
  `std::unique_ptr`, move the smart pointer directly. Do not `release()` and
  re-wrap just to transfer ownership.
- Use `release()` only at a real raw-pointer ownership boundary that cannot
  accept a smart pointer. If the next owner is another smart pointer in
  project code, redesign the handoff to use `std::move`.

```cpp
// Bad: stored reference may dangle if constructed from a temporary.
class Foo {
 public:
  explicit Foo(const std::string& text) : text_(text) {}

 private:
  const std::string& text_;
};

// Good: own storage directly.
class Foo {
 public:
  explicit Foo(std::string text) : text_(std::move(text)) {}

 private:
  std::string text_;
};
```

```cpp
// Bad: redundant raw `new`.
std::unique_ptr<Widget> widget(new Widget(config));

// Good: direct heap allocation with ownership in the type.
auto widget = std::make_unique<Widget>(config);
```

```cpp
// Bad: unwrap and re-wrap ownership between smart pointers.
owner_.reset(tmp.release());

// Good: move ownership directly.
owner_ = std::move(tmp);
```

```cpp
// Good: wrap legacy raw ownership immediately.
std::unique_ptr<Widget> widget(CreateOwnedWidget());
```

```cpp
// Good: build replacement resources locally, then move them into members only
// after the full setup succeeds.
SetupState setup;
if (const Status setup_status = BuildSetup(setup); !setup_status.ok()) {
  return setup_status;
}
audio_client_ = std::move(setup.audio_client);
format_ = std::move(setup.format);
```

### Sentinel values
- Do not encode missing or invalid states as magic values (`-1`, `INT_MIN`,
  `NaN`). Use `std::optional<T>` for absence or an error-carrying result type
  when error details are needed.

```cpp
// Bad: sentinel in value domain.
int AccountBalance();  // returns -5 when account is closed.

// Good: absence is explicit.
std::optional<int> AccountBalance();
```

### Initialization
- Prefer designated initializers when constructing aggregates (structs with no
  user-declared constructors). They make each field's purpose visible at the
  call site and prevent silent transposition errors.
- Omit designated initializers only when the struct has a single field or when
  every field is obvious from positional context and the call site is
  immediately adjacent to the struct definition.

```cpp
// Bad: positional -- easy to swap gain and frequency silently.
const ShelfParams params = {12.0, 100.0, 48000.0};

// Good: each field is named at the call site.
const ShelfParams params = {.gain_db = 12.0,
                            .freq_hz = 100.0,
                            .sample_rate = 48000.0};
```

```cpp
// Good: single-field struct -- designator adds noise, not clarity.
const Wrapper w = {value};
```

### Avoid brittle parameters
- Prefer typed parameters over strings or raw integers for values that control
  behavior. Strings used as mode selectors fail silently on typos, case
  mismatches, and trailing spaces.
- Reserve strings for data that genuinely originates as text (for example, file
  paths and user-visible labels).
- If an integer is acting as a mode selector, replace it with an enum or a
  named constant.

```cpp
// Bad: typo "enabeld" compiles and silently does nothing.
void SetMode(const std::string& mode);  // "enabled" / "disabled"

// Good: wrong value is a compile error.
enum class Mode { kEnabled, kDisabled };
void SetMode(Mode mode);

// Also good for binary flags.
void SetEnabled(bool enabled);
```

### Strings and API boundaries
- Prefer `std::string_view` and `std::wstring_view` for read-only inputs.
- Prefer `std::string` and `std::wstring` for owned storage.
- Avoid raw `const char*` and `const wchar_t*` in project interfaces.
- When a platform API requires a NUL-terminated string, pass `c_str()` from an
  owning string.
- Do not rely on `string_view::data()` for NUL termination.

```cpp
// Bad: raw pointers in project interface.
void SetEndpointName(const wchar_t* name);

// Good: project interface uses string_view.
void SetEndpointName(std::wstring_view name);
```

```cpp
// Bad: data() from string_view may not be NUL-terminated.
void OpenDevice(std::wstring_view endpoint_id) {
  ::OpenEndpoint(endpoint_id.data());
}

// Good: build an owning string, then pass c_str().
void OpenDevice(std::wstring_view endpoint_id) {
  const std::wstring endpoint_id_str(endpoint_id);
  ::OpenEndpoint(endpoint_id_str.c_str());
}
```

### Commits
- Keep commits small and single-purpose (target: <=100 changed lines when
  practical).
- Include corresponding tests in the same commit as behavior changes.
- Commit messages should include:
  - Why the change is needed, such as what would happen if not added.
  - Main alternatives considered and why they were not chosen.
  - When the change modifies existing behavior or structure, state the before
    and after so the reviewer can understand the delta without reading the
    diff.

```text
Good commit sequence:
1) Add decode-strategy interface and wiring (+tests).
2) Add mono decode behavior (+tests).
3) Add stereo decode behavior (+tests).
```

```text
Bad commit message (no before/after for a structural change):
Refactor decode dispatch.

Why:
- Repeated format checks are slow.

Good commit message:
Refactor decode dispatch to one-time format selection.

Before: every sample hits a format switch in the inner loop.
After: format is resolved once before the loop; inner loop calls
the selected decoder directly.

Why:
- Removes repeated per-sample format checks from the hot loop.

Alternatives considered:
- Keep repeated switch in DecodeSample (rejected: duplicates logic).
- Runtime polymorphism in hot loop (rejected: extra indirection cost).
```

### Testing
- Every new source file (`*.cpp` / `*.hpp`) must have a corresponding
  `*_test.cpp`. Add the test file and CMake wiring in the same change that
  introduces the source file. If the module's public API touches hardware or
  COM, test the observable contract (status codes, output pointer state,
  error messages) by calling the real function under controlled conditions
  (for example, COM initialized vs. uninitialized).
- Prefer `TEST()` over `TEST_F()`. Only use a fixture when `SetUp()` and
  `TearDown()` genuinely manage a resource lifecycle (for example, creating and
  destroying a real Win32 window). Sharing a constant or a trivially
  constructed object is not a reason for a fixture.

```cpp
// Bad: fixture just to share a constant.
class ToneFilterTest : public ::testing::Test {
 protected:
  static constexpr double kRateHz = 48000.0;
};
TEST_F(ToneFilterTest, GainIsZero) { ... }

// Good: constant declared inline where it is needed.
TEST(ToneFilterTest, GainIsZero) {
  ToneFilter filter(48000.0);
  ...
}
```

- In `*_test.cpp`, if the paired production header uses a named namespace, wrap
  the tests in that same namespace.
- Use PascalCase for test suite names (the first argument to `TEST()` and
  `TEST_F()`).

```cpp
// Bad: snake_case suite name.
TEST(theme_manager_test, BlendColorAtT0ReturnsBase) { ... }

// Good: PascalCase suite name.
TEST(ThemeManagerTest, BlendColorAtT0ReturnsBase) { ... }
```

- Prefer `EXPECT_*` over `ASSERT_*`. Use `ASSERT_*` only when the remainder of
  the test is meaningless if the condition fails.

```cpp
// Bad: first failure aborts; later assertions never run.
ASSERT_GT(filter.gain_db(), 0.0);
ASSERT_LT(filter.gain_db(), ToneFilter::kMaxLevelDb);

// Good: all failures reported in one run.
EXPECT_GT(filter.gain_db(), 0.0);
EXPECT_LT(filter.gain_db(), ToneFilter::kMaxLevelDb);

// ASSERT_* only when later code would crash without this:
ASSERT_NE(hwnd, nullptr);  // the rest of the test dereferences hwnd.
```

- Keep `EXPECT_*` and `ASSERT_*` at top-level `TEST()` and `TEST_F()` scope.
  Avoid putting them in helper functions, loops, constructors or destructors,
  or fixture `SetUp()` and `TearDown()`.

```cpp
// Bad: assertion in helper hides call-site context.
void ExpectValidRange(int min, int max) {
  ASSERT_LT(min, max);
}
TEST(ControlTest, SliderRange) {
  ExpectValidRange(1000, 0);
}

// Good: helper returns data; assertion stays in the test body.
bool IsValidRange(int min, int max) {
  return min < max;
}
TEST(ControlTest, SliderRange) {
  ASSERT_TRUE(IsValidRange(0, 1000));
}
```

- Avoid `EXPECT_*` and `ASSERT_*` inside loops. Use matchers or parameterized
  tests so failures include clear element and case context.

```cpp
// Bad: failure in loop gives weak context.
for (int value : values) {
  EXPECT_EQ(value, 1);
}

// Good: matcher reports which element failed.
EXPECT_THAT(values, ::testing::Each(::testing::Eq(1)));
```

- Do not introduce a single-use variable just to pass it to an assertion.
  Inline the expression so failure messages show the actual code that produced
  the value.

```cpp
// Bad: extra variable obscures what is being tested.
const double gain = filter.gain_db();
EXPECT_NEAR(gain, 12.0, 0.001);

// Good: failure message shows the actual expression.
EXPECT_NEAR(filter.gain_db(), 12.0, 0.001);
```

- Include only the data each test needs. Irrelevant setup buried in helpers
  hides what the test is actually checking.

```cpp
// Bad: reader must chase SetUpFilter to know what it does.
TEST(ToneFilterTest, ControlAtMax) {
  ToneFilter filter(48000.0);
  SetUpFilter(filter);  // does it reset? warm up? set gain?
  filter.SetControlPosition(0.0);
  EXPECT_NEAR(filter.gain_db(), ToneFilter::kMaxLevelDb, 0.001);
}

// Good: everything the test needs is right here.
TEST(ToneFilterTest, ControlAtMax) {
  ToneFilter filter(48000.0);
  filter.SetControlPosition(0.0);
  EXPECT_NEAR(filter.gain_db(), ToneFilter::kMaxLevelDb, 0.001);
}
```

- Do not overuse mocks. A test full of mock expectations exposes
  implementation details and is brittle. Mock only true boundaries (for
  example, `AudioEngineInterface`) where a real implementation would open a
  device or call a network.

```cpp
// Bad: mocking the DSP filter couples the test to private call patterns.
MockToneFilter mock;
EXPECT_CALL(mock, SetControlPosition(0.5));

// Good: mock only the device boundary; let the real filter run.
class FakeAudioEngine : public AudioEngineInterface {
 public:
  void SetControlPosition(double pos) override { last_pos_ = pos; }
  double last_pos_ = 0.0;
};
```

- Test the contract, meaning observable behavior the component promises, not
  internal details that could change without breaking a real guarantee.

```cpp
// Bad: tests a specific internal coefficient value.
TEST(ToneFilterTest, B0CoefficientValue) {
  ToneFilter filter(48000.0);
  filter.SetLevelDb(12.0);
  EXPECT_NEAR(filter.coefficients().b0, 1.006, 0.001);
}

// Good: tests the observable contract.
TEST(ToneFilterTest, BassBoostedAt12dB) {
  ToneFilter filter(48000.0);
  filter.SetLevelDb(12.0);
  const FilterCoefficients& c = filter.coefficients();
  EXPECT_GT((c.b0 + c.b1 + c.b2) / (1.0 + c.a1 + c.a2), 1.5);
}
```

- Prefer testing through the public interface. If the interface is too narrow
  for sufficient coverage, extract a small testable subcomponent rather than
  punching through encapsulation.
- When a test genuinely needs private access, prefer this order:
  (1) extend the public interface (add a `ForTesting` method if truly
  test-only), then (2) define a friend test peer class in `_test.cpp` for
  controlled access. Never use `FRIEND_TEST`, and never befriend an entire
  fixture.

```cpp
// Bad: FRIEND_TEST couples the header to specific test names.
class ToneFilter {
  FRIEND_TEST(ToneFilterTest, InternalCoefficients);
};

// Good: test peer in _test.cpp gives controlled, named access.
class ToneFilterPeer {
 public:
  explicit ToneFilterPeer(ToneFilter& f) : f_(f) {}
  const FilterCoefficients& coefficients() const { return f_.coefficients(); }

 private:
  ToneFilter& f_;
};
```

- Each test has exactly one arrange -> act -> assert. Do not call the function
  under test more than once in one test. Put each scenario in its own test.

```cpp
// Bad: two scenarios share one test.
TEST(ToneFilterTest, SliderExtremes) {
  constexpr double kSampleRateHz = 48000.0;
  constexpr double kZeroPosition = 0.0;
  constexpr double kOnePosition = 1.0;
  constexpr double kTolerance = 0.001;
  ToneFilter filter(kSampleRateHz);
  filter.SetControlPosition(kZeroPosition);
  EXPECT_NEAR(filter.gain_db(), ToneFilter::kMaxLevelDb, kTolerance);
  filter.SetControlPosition(kOnePosition);  // second act; should not be here.
  EXPECT_NEAR(filter.gain_db(), kZeroPosition, kTolerance);
}

// Good: one scenario per test.
TEST(ToneFilterTest, ControlAtZeroSetsMaxLevel) {
  constexpr double kSampleRateHz = 48000.0;
  constexpr double kZeroPosition = 0.0;
  constexpr double kTolerance = 0.001;
  ToneFilter filter(kSampleRateHz);
  filter.SetControlPosition(kZeroPosition);
  EXPECT_NEAR(filter.gain_db(), ToneFilter::kMaxLevelDb, kTolerance);
}
TEST(ToneFilterTest, ControlAtOneSetsZeroLevel) {
  constexpr double kSampleRateHz = 48000.0;
  constexpr double kOnePosition = 1.0;
  constexpr double kZeroPosition = 0.0;
  constexpr double kTolerance = 0.001;
  ToneFilter filter(kSampleRateHz);
  filter.SetControlPosition(kOnePosition);
  EXPECT_NEAR(filter.gain_db(), kZeroPosition, kTolerance);
}
```

- Duplication in tests is more acceptable than in production code. It is fine
  to repeat setup boilerplate across tests; do not factor out the core story of
  a test into a helper that the reader must chase.
- If testing an API requires large amounts of boilerplate, treat that as an API
  usability signal, not a reason to add more test infrastructure.

### Working with lint rules and tests
- After every code change, fix all clang-tidy warnings and clang-format errors
  in the affected files before considering the task done. Do not leave warnings
  for a follow-up.
- If the only way to resolve a clang-tidy warning, clang-format issue, or test
  failure is to suppress or relax the rule (for example, adding `// NOLINT`,
  removing a check from `.clang-tidy`, changing `.clang-format`, or deleting a
  test), stop and ask before proceeding.
- The same applies when auditing existing suppressions. Before adding a
  justification comment to a bare `// NOLINT`, ask whether the warning can be
  eliminated entirely first.
- When a suppression is warranted, verify the reason applies to each identifier
  individually; do not extend it by proximity.
- Never copy or duplicate declarations, types, or variables to work around
  ordering or dependency issues. That creates two sources of truth that can
  silently diverge. If lint or build rules need adjustment, ask first.

```cpp
// Bad: suppression by proximity.
const double x = ...;   // NOLINT(readability-identifier-length) - cookbook
const double y = ...;   // NOLINT(readability-identifier-length) - cookbook
const double dl = ...;  // NOLINT(readability-identifier-length)

// Good: keep justified names; rename unjustified abbreviations.
const double x = ...;      // NOLINT(readability-identifier-length) - cookbook
const double y = ...;      // NOLINT(readability-identifier-length) - cookbook
const double delay = ...;  // no suppression needed
```

### Keeping everything current
- After every rename, move, or semantic change, audit all affected files for
  stale artifacts before considering the task done. This is not limited to
  comments -- check every category:
  - **Names**: member names, variable names, parameter names, and type names
    that reference an old concept. A member called `capture_format` that
    actually holds a render format is stale even if the code compiles.
  - **Comments**: inline comments, struct and class descriptions, function
    declaration comments, data member comments, and file headers that
    reference old names, old relationships, or old behavior.
  - **Documentation**: `CLAUDE.md` examples, `README.md` references, and any
    other prose that mentions the changed element.
  - **Missing documentation**: every new or renamed code element (struct,
    class, function, data member, file) must have a comment that follows the
    comment guidelines. If a rename introduces a new concept (for example
    `service` replacing `audio_capture_client`), the comment must explain
    what the new name means in context.
- Apply this consistently to source files, headers, tests, and `CLAUDE.md`.

```cpp
// Bad: member name references a concept that no longer exists. The field
// actually holds the render mix format, but the name implies a separate
// capture format.
WAVEFORMATEX* capture_format;

// Good: name matches what the field holds; comment explains the dual role.
// Render mix format reused as the capture format; process loopback
// captures in whatever format the render endpoint uses.
WAVEFORMATEX* format;

// Bad: struct added without explaining what it groups.
struct StreamClientSetup {
  CaptureClientSetup capture;
  RenderClientSetup render;
};

// Bad: data member added without explaining why it is a member.
std::atomic<double> gain_db_ = 0.0;

// Bad: function added without a declaration comment.
[[nodiscard]] bool StartStreams(RunningPipelineState& state,
                                std::stop_token stoken) {
  ...
}

// Bad: behavior changed but comment still describes the old contract.
// Returns the raw device pointer.
[[nodiscard]] EndpointAcquisition AcquireEndpoint() {
  ...
}

// Good: comment matches the current behavior and return contract.
// Returns true when both capture and render streams are running; returns
// false when startup failed and recovery was unsuccessful.
[[nodiscard]] bool StartStreams(RunningPipelineState& state,
                                std::stop_token stoken) {
  ...
}

// Good: data member explains why atomic is needed.
// Atomic so gain can be updated from any thread while audio is processing.
std::atomic<double> gain_db_ = 0.0;
```

### After updating CLAUDE.md
- Before adding a new rule, check for conflicts and duplication with existing
  rules in this file, the Google C++ Style Guide, and any other guides this
  document references. If the new rule is a special case of an existing one,
  add it as a clarification or heuristic under that rule rather than as a
  standalone entry.
- Every new rule must include at least one example showing the bad pattern and
  the good pattern.
- After changing `CLAUDE.md`, audit the codebase for violations of the
  added or tightened rules and fix them in the same task.
- For style-enforcement changes, verify `.clang-format`, `.clang-tidy`, and
  relevant build config still match the documented rules.
- Build and run tests before considering a `CLAUDE.md`-driven update complete.
