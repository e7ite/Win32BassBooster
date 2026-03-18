// Verifies the two externally visible contracts of loopback activation:
// success with COM initialized, and failure without COM initialized. Each
// test checks the full returned status rather than splitting one call into
// several narrower tests.
// The remaining uncovered lines are private callback and error-formatting
// branches inside the async completion handler. Reaching them would require
// testing implementation details directly instead of the public activation
// contract exposed by this file. We do not take that extra step here because
// it would couple the tests to the handler internals rather than the public
// activation contract this file is meant to verify.

#include "loopback_capture_activation.hpp"

#include <objbase.h>

#include <string>

#include "gtest/gtest.h"

TEST(LoopbackCaptureActivationTest, FailsWithoutComInitialization) {
  // Without COM initialization, activation must fail and fully describe that
  // failure through the returned status object.
  IAudioClient* client = nullptr;

  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(client, nullptr);
  EXPECT_TRUE(FAILED(status.code));
  EXPECT_FALSE(status.error_message.empty());
  EXPECT_NE(status.error_message.find(L"0x"), std::wstring::npos);
}

TEST(LoopbackCaptureActivationTest, SucceedsWithComInitialized) {
  // Keep COM setup and teardown explicit in the test body so the lifetime is
  // obvious and there is no helper logic hiding cleanup behavior.
  // `S_FALSE` means COM was already initialized on this thread; both `S_OK`
  // and `S_FALSE` pass the `SUCCEEDED` check.
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init));

  IAudioClient* client = nullptr;

  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  ASSERT_TRUE(status.ok());
  EXPECT_EQ(status.code, S_OK);
  ASSERT_NE(client, nullptr);

  client->Release();

  CoUninitialize();
}
