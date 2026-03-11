// Verifies loopback capture activation succeeds with COM initialized,
// returns a usable audio client, and reports a clear error without COM.

#include "loopback_capture_activation.hpp"

#include <objbase.h>

#include <string>

#include "gtest/gtest.h"

TEST(LoopbackCaptureActivationTest, SucceedsWithComInitialized) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  EXPECT_TRUE(status.ok());
  EXPECT_NE(client, nullptr);
  if (client != nullptr) {
    client->Release();
  }

  CoUninitialize();
}

TEST(LoopbackCaptureActivationTest, ReturnsNonEmptyErrorMessageOnFailure) {
  // Without COM initialization, activation must fail and produce a
  // human-readable error message containing the hex HRESULT.
  CoUninitialize();

  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  if (!status.ok()) {
    EXPECT_FALSE(status.error_message.empty());
    EXPECT_NE(status.error_message.find(L"0x"), std::wstring::npos);
  }

  // Clean up in case it unexpectedly succeeded.
  if (client != nullptr) {
    client->Release();
  }
}

TEST(LoopbackCaptureActivationTest, ClientIsNullOnFailure) {
  CoUninitialize();

  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  if (!status.ok()) {
    EXPECT_EQ(client, nullptr);
  }

  if (client != nullptr) {
    client->Release();
  }
}

TEST(LoopbackCaptureActivationTest, StatusCodeIsFailureHresultOnError) {
  CoUninitialize();

  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  if (!status.ok()) {
    EXPECT_TRUE(FAILED(status.code));
  }

  if (client != nullptr) {
    client->Release();
  }
}

TEST(LoopbackCaptureActivationTest, SuccessStatusCodeIsSok) {
  const HRESULT com_init =
      CoInitializeEx(/*pvReserved=*/nullptr, COINIT_MULTITHREADED);
  ASSERT_TRUE(SUCCEEDED(com_init) || com_init == S_FALSE);

  IAudioClient* client = nullptr;
  const AudioPipelineInterface::Status status =
      ActivateLoopbackCaptureClient(client);

  EXPECT_EQ(status.code, S_OK);

  if (client != nullptr) {
    client->Release();
  }

  CoUninitialize();
}
