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
- Add inline parameter-name labels (`/*param=*/`) whenever the argument's
  role is not immediately obvious at the call site. This includes:
  - Literal values (`0`, `nullptr`, `FALSE`) whose meaning depends on
    position.
  - Expressions that lose their semantic meaning after unpacking, such as
    `LOWORD(lparam)` or `client_rect.right` passed as a width.
  - Calls where multiple parameters share the same type, making it easy to
    silently transpose arguments.
- Skip parameter labels only when the argument's role is obvious from the
  name or type alone (for example, `memset(buffer, 0, size)`, or a
  single-parameter call like `DeleteObject(font)`).
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
// Bad: expressions lose their meaning after unpacking; reader must check
// the signature to know which is width and which is height.
UpdateLayout(hwnd, slider_hwnd_, LOWORD(lparam), HIWORD(lparam),
             &header_rc_, &slider_label_rc_, &footer_rc_);

// Good: labels make the mapping visible at the call site.
UpdateLayout(hwnd, slider_hwnd_, /*width=*/LOWORD(lparam),
             /*height=*/HIWORD(lparam), &header_rc_, &slider_label_rc_,
             &footer_rc_);
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

- Place each data member comment directly above the single member it describes.
  Do not write one comment that covers multiple members; IDE hover and
  navigation show only the comment immediately above a declaration.

```cpp
// Bad: one comment covers two members; hovering on z2_ shows nothing.
// Per-channel filter memory for continuous output across buffers.
std::array<double, kChannels> z1_ = {};
std::array<double, kChannels> z2_ = {};

// Good: each member has its own comment visible on hover.
// Per-channel memory of the previous intermediate value.
std::array<double, kChannels> z1_ = {};
// Per-channel memory of the two-samples-ago intermediate value.
std::array<double, kChannels> z2_ = {};
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

### Code structure and refactoring
- These rules apply to both new code and refactors unless a rule says
  otherwise.
- Never leave dead code in the codebase. Remove unreachable functions,
  unused variables, stale flags, obsolete branches, and any code that is
  defined but never called. Do this in the same change that makes it dead,
  not in a follow-up.
- This applies at every scale: a single unused variable, a function no
  caller invokes, and an entire module (`.hpp`, `.cpp`, `_test.cpp`, CMake
  target) that nothing in the live code path uses. When a module is wired
  into the build and passed around in structs or function parameters but
  never actually called in the processing path, it is dead code. Remove
  the source files, the CMake library and test targets, the link
  dependency, all include directives, struct fields, function parameters,
  member variables, and tests that exercise the removed module.
- Minimize scope at every level. Keep logic in the narrowest scope that works:
  local variable -> anonymous namespace -> `private` member ->
  `public` member or free function in a header. Promote scope only when
  something genuinely needs to be shared.
- When splitting long functions, prefer anonymous-namespace functions with
  explicit inputs and outputs before adding new member functions.
- Keep extracted helpers explicit: prefer clear inputs and outputs over hidden
  shared state.
- In production code, prefer a single source of truth for behavior, mapping,
  and policy. When the same logic must stay in sync across multiple live code
  paths, extract shared code or shared data instead of copying it.
- Do not introduce indirection only to satisfy DRY when the shared form is
  harder to read than the repeated code. Keep the existing test guidance:
  small repeated setup in tests is acceptable when it keeps the scenario clear.
- When several local rules or heuristics pull in different directions, prefer
  the smallest refactor that satisfies all of them together.
- Do not apply one heuristic mechanically when it would create a worse
  violation of other readability or maintainability rules, such as function
  length, nesting depth, explicit data flow, or scanability.
- If no reasonable local refactor can satisfy the competing rules, stop and
  ask before choosing which rule to violate.
- When clang-tidy warns that a member function could be made `static`
  (`readability-convert-member-functions-to-static`), do not add `static`.
  Instead, remove the function from the class entirely. If the body is short
  and has only one caller, inline it at the call site. Otherwise extract it
  as a free function: anonymous-namespace helper if used only within the
  `.cpp`, or a declared free function in the header if called from other
  translation units.

```cpp
// Bad: clang-tidy suggests making Run() static; adding static keeps it in
// the class without reason.
class MainWindow {
 public:
  static int Run();  // does not use any member state.
};

// Good: short body with one caller -- inline at the call site.
int WINAPI wWinMain(...) {
  ...
  MSG msg = {};
  while (GetMessageW(&msg, ...) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

// Good: longer body or multiple callers -- anonymous-namespace helper.
namespace {
FormatInfo BuildFormatInfo(const WAVEFORMATEX& fmt) { ... }
}  // namespace
```

```cpp
// Bad: clamp policy duplicated across production paths; a future range change
// can update one copy and forget the other.
void SetBassBoostDb(double gain_db) {
  if (gain_db < 0.0) {
    gain_db = 0.0;
  }
  if (gain_db > kMaxGainDb) {
    gain_db = kMaxGainDb;
  }
  bass_boost_db_ = gain_db;
}

void SetSavedBassBoostDb(double gain_db) {
  if (gain_db < 0.0) {
    gain_db = 0.0;
  }
  if (gain_db > kMaxGainDb) {
    gain_db = kMaxGainDb;
  }
  saved_bass_boost_db_ = gain_db;
}

// Good: shared helper keeps the policy in one place with explicit inputs and
// outputs.
namespace {
double ClampBassBoostDb(double gain_db) {
  return std::clamp(gain_db, 0.0, kMaxGainDb);
}
}  // namespace

void SetBassBoostDb(double gain_db) {
  bass_boost_db_ = ClampBassBoostDb(gain_db);
}

void SetSavedBassBoostDb(double gain_db) {
  saved_bass_boost_db_ = ClampBassBoostDb(gain_db);
}
```

```cpp
// Bad: mechanically following the "inline short single-caller bodies" rule
// makes the caller longer, adds nesting, and makes the control flow harder to
// scan.
void StartRenderLoop() {
  ...
  if (needs_reset) {
    ...
    if (should_commit) {
      ...
    }
  }
  ...
  // Inlined from BuildRenderSetup().
  if (format == nullptr) {
    return;
  }
  ...
}

// Good: satisfy the competing rules together by keeping the caller short and
// extracting explicit helper logic.
namespace {
RenderSetup BuildRenderSetup(const DeviceState& state,
                             const WAVEFORMATEX* format) {
  ...
}
}  // namespace

void StartRenderLoop() {
  ...
  const RenderSetup render_setup = BuildRenderSetup(state, format);
  ...
}
```

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

- Use ownership (`std::unique_ptr`) for composition, where the owned object
  is a part of the owner. Use non-owning pointers for collaboration, where
  two independent components communicate but neither is a part of the other.
  If a class only calls methods on a dependency without controlling when it
  is created or destroyed, that is collaboration, not composition.

```cpp
// Bad: MainWindow takes ownership of the pipeline, implying the pipeline
// is a part of the window. But they are independent concerns -- the window
// just forwards UI events.
class MainWindow {
 public:
  explicit MainWindow(std::unique_ptr<AudioPipelineInterface> audio);

 private:
  std::unique_ptr<AudioPipelineInterface> audio_;
};

// Good: non-owning pointer expresses collaboration. The caller owns the
// pipeline and controls its lifetime.
class MainWindow {
 public:
  // Does not take ownership; the pipeline must outlive the window.
  explicit MainWindow(AudioPipelineInterface* audio);

 private:
  AudioPipelineInterface* audio_ = nullptr;  // borrowed
};
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

- When constructing a struct inline (as a function argument or return value
  rather than in a variable declaration), always name the type explicitly.
  Omitting the type forces the reader to look up the function signature to
  know what is being constructed.

```cpp
// Bad: reader must check PaintWindow's signature to know the type.
PaintWindow(hwnd, {.palette = palette_,
                   .header_rc = header_rc_});

// Good: type is visible at the call site.
PaintWindow(hwnd, PaintContext{.palette = palette_,
                               .header_rc = header_rc_});

// Good: variable declaration already names the type on the left-hand side.
const PaintContext ctx = {.palette = palette_,
                          .header_rc = header_rc_};
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

- When a function has two or more adjacent parameters of the same type,
  transposition is silent and hard to catch in review. Group related same-type
  parameters into a named struct so callers use designated initializers and the
  compiler catches transposition.

```cpp
// Bad: width and height are both int; swapping them compiles silently.
void UpdateLayout(int width, int height);
UpdateLayout(client_rect.right, client_rect.bottom);

// Good: struct with named fields makes transposition a compile error.
struct ClientSize {
  int width;
  int height;
};
void UpdateLayout(ClientSize client);
UpdateLayout(ClientSize{.width = client_rect.right,
                        .height = client_rect.bottom});
```

- The same applies to output parameters: when a function writes to multiple
  pointers of the same type, return a struct by value instead.

```cpp
// Bad: three RECT* outputs are easily transposed.
void ComputeLayout(RECT* header, RECT* body, RECT* footer);

// Good: return a struct; fields are named and order does not matter.
struct LayoutRegions {
  RECT header;
  RECT body;
  RECT footer;
};
LayoutRegions ComputeLayout();
```

- Do not wrap parameters when the types already differ enough that the
  compiler would catch a transposition, or when the function signature is
  dictated by a platform callback (for example, `WNDPROC`).

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

### Using declarations
- In `.cpp` files, add `using` declarations for repeatedly qualified names to
  reduce noise. Place them after includes and before the anonymous namespace.
- Do not add `using` declarations or `using namespace` directives in header
  files; they leak into every translation unit that includes the header.

```cpp
// Bad in a .cpp file: repeated qualification adds visual noise.
palette_ = theme_manager::BuildPalette();
theme_manager::ApplyTitleBarTheme(hwnd_);

// Good: using declarations at file scope in the .cpp.
using theme_manager::ApplyTitleBarTheme;
using theme_manager::BuildPalette;
// ...
palette_ = BuildPalette();
ApplyTitleBarTheme(hwnd_);
```

```cpp
// Bad in a header: leaks into every includer.
using theme_manager::Palette;  // pollutes all translation units
class MainWindow { ... };
```

### Commits
- Every commit must build and pass all existing and new tests. Do not commit
  code that breaks the build or causes test failures, even temporarily.
- Keep commits small and single-purpose (target: <=100 changed lines when
  practical).
- Include corresponding tests in the same commit as behavior changes.
- Never refer to deleted, squashed, or rewritten-away commits in later commit
  messages, PR text, or documentation. If history changes, rewrite later
  commit messages too so every commit reference still exists in visible
  history.
- In commit messages, follow the same identifier-marking rule used in code
  comments: wrap real identifiers, file paths, commands, APIs, and test names
  in backticks.
- Never run `git push` unless the user explicitly asks for that step in the
  current conversation. A request to fix, finish, or commit work is not
  permission to push.
- Commit messages should include:
  - Why the change is needed, such as what would happen if not added.
  - Main alternatives considered and why they were not chosen.
  - When the change modifies existing behavior or structure, state the before
    and after so the reviewer can understand the delta without reading the
    diff. Omit before/after when the change is simple enough that the diff
    already communicates the delta (for example, a one-line wording fix or a
    search-and-replace rename).

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

```text
Bad commit message:
Follow up to abc1234 after the rebase.

Why:
- FakeAudioDevice replaces the live-device dependency in
  audio_pipeline_test.cpp.

Good commit message:
Follow up to `1234abcd` in current history.

Why:
- `FakeAudioDevice` replaces the live-device dependency in
  `audio_pipeline_test.cpp`.
```

```text
Bad:
- Fix the branch, then run git push origin main without asking.

Good:
- Fix the branch, then ask whether to run `git push origin main`.
```

### Testing
- When production code collaborates with OS APIs, service APIs, device APIs,
  filesystems, clocks, threads, process state, or other environment-bound
  behavior, first prefer extracting an injectable boundary and testing against
  a controllable test double. Use a focused integration test only when the
  real boundary is itself part of the contract under test.
- Every new behavior-owning source file should have corresponding test
  coverage in the same change, including the needed `*_test.cpp` and CMake
  wiring when a standalone test target is the clearest fit. Do not add
  placeholder tests for pure abstract interfaces, passive transfer types, or
  declaration-only headers with no behavior; cover those through the concrete
  modules and contract tests that exercise them. If the module's public API is
  a thin wrapper around hardware, COM, or another system boundary, add at
  least one focused test of the observable contract (status codes, output
  pointer state, error messages) under controlled conditions.
- Never remove, disable, or weaken a test to make CI pass. A failing test is a
  signal to fix production code, fix the test environment, or add the missing
  injectable boundary or test double -- not to delete the coverage.

```cpp
// Bad: test adds control flow to adapt to runtime environment details.
TEST(PipelineTest, StartSucceeds) {
  AudioPipeline pipeline;
  const auto status = pipeline.Start();
  if (!status.ok()) {
    EXPECT_TRUE(FAILED(status.code));
    return;  // silently passes without testing the contract
  }
  EXPECT_TRUE(pipeline.is_running());
}

// Bad: test removed because it failed in one environment.

// Good: inject a test double so the contract stays deterministic.
class MockAudioDevice {
 public:
  std::unique_ptr<AudioDevice> CreateAudioDevice();
  void set_open_succeeds(bool succeeds);
};

TEST(AudioPipelineTest, StartSucceedsWithInjectedDevice) {
  MockAudioDevice device;
  AudioPipeline pipeline(device.CreateAudioDevice());
  ASSERT_TRUE(pipeline.Start().ok());
  EXPECT_TRUE(pipeline.is_running());
}
```
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
- Tests must be order-independent. Every test must pass when run alone, first,
  last, or under shuffled execution.
- Do not rely on ambient state left behind by another test, such as COM
  initialization, current working directory, environment variables, message
  queues, temp files, registry state, singletons, or static mutable caches.

```cpp
// Bad: second test only passes because another test initialized COM first.
TEST(LoopbackCaptureActivationTest, ActivationSucceeds) {
  IAudioClient* client = nullptr;
  EXPECT_TRUE(ActivateLoopbackCaptureClient(client).ok());
}

// Good: the test owns the state it needs.
TEST(LoopbackCaptureActivationTest, ActivationSucceeds) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  IAudioClient* client = nullptr;
  ASSERT_TRUE(ActivateLoopbackCaptureClient(client).ok());
  ASSERT_NE(client, nullptr);

  client->Release();
  CoUninitialize();
}
```

- If a test changes process-wide, thread-local, or OS resource state, the same
  test or fixture that set it up must restore it.
- Never call teardown APIs for state the test did not create. "Resetting" the
  environment by blindly calling cleanup functions is brittle because it depends
  on unknown prior state.

```cpp
// Bad: tries to force a failure path by tearing down unknown ambient state.
TEST(LoopbackCaptureActivationTest, FailsWithoutCom) {
  CoUninitialize();
  ...
}

// Good: failure path is exercised without mutating unrelated test state.
TEST(LoopbackCaptureActivationTest, FailsWithoutCom) {
  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);
  ASSERT_FALSE(status.ok());
}
```

- Avoid brittle synchronization and test-only control flow. Do not add sleeps,
  wall-clock waits, random behavior, or complex helper runtimes to make a test
  pass.
- Prefer direct, deterministic triggers (`SendMessageW`, explicit inputs, fixed
  seeds when unavoidable) and explicit cleanup in the test body or fixture.
- If a test helper starts needing branches, threads, promises, retries, or
  hidden cleanup, simplify the test coverage or extract a better production
  boundary instead of building a mini runtime in `_test.cpp`.
- Do not add logic to tests unless it is absolutely unavoidable. If it seems
  unavoidable, ask before adding it.
- If a test needs branches, loops, retries, conditional cleanup, or helper
  runtimes to express one scenario, simplify the test or extract a better
  production boundary instead. Otherwise the test starts needing validation of
  its own and becomes brittle.

```cpp
// Bad: helper complexity starts to dominate the test story.
ActivationResult RunOnWorkerThread(bool initialize_com, int retry_count);

// Good: the test drives one deterministic scenario directly.
SendMessageW(window.hwnd(), WM_HSCROLL, ..., ...);
EXPECT_NEAR(pipeline.last_boost_level(), 0.5, 1e-6);
```

```cpp
// Bad: the test contains its own control flow and conditional cleanup.
TEST(LoopbackCaptureActivationTest, FailureStatusHasMessage) {
  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  if (!status.ok()) {
    EXPECT_FALSE(status.error_message.empty());
  }

  if (status.ok() && client != nullptr) {
    client->Release();
  }
}

// Good: the test states the contract directly.
TEST(LoopbackCaptureActivationTest, FailureStatusHasMessage) {
  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  ASSERT_FALSE(status.ok());
  EXPECT_FALSE(status.error_message.empty());
}
```

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
