// Verifies the pipeline's initial state, boost level clamping, gain curve
// shape, and start/stop lifecycle through an injected test audio device.
// This target still reports lower aggregate coverage than the pipeline logic
// itself because it links the default `WasapiAudioDevice` path. The remaining
// gap is the real COM/default-endpoint startup flow, which would require live
// hardware integration tests or deeper DI below `AudioDevice`. We do not take
// that extra step here because live hardware makes the suite environment-
// dependent, and deeper DI would push test scaffolding into the concrete
// WASAPI adapter.

#include "audio_pipeline.hpp"

#include <audioclient.h>

#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <utility>

#include "audio_device.hpp"
#include "bass_boost_filter.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Each;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

class MockAudioDevice final : public AudioDevice {
 public:
  MOCK_METHOD(AudioPipelineInterface::Status, Open, (), (override));

  MOCK_METHOD(AudioPipelineInterface::Status, StartStreams, (), (override));

  MOCK_METHOD(void, StopStreams, (), (override));

  MOCK_METHOD(void, Close, (), (override));

  MOCK_METHOD(CapturePacket, ReadNextPacket, (), (override));

  MOCK_METHOD(HRESULT, WriteRenderPacket, (std::span<const float> pcm),
              (override));

  MOCK_METHOD(bool, TryRecover, (HRESULT failure), (override));

  MOCK_METHOD(double, sample_rate, (), (const, override));

  MOCK_METHOD(const std::wstring&, endpoint_name, (), (const, override));
};

TEST(AudioPipelineTest, NotRunningBeforeStart) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, DefaultConstructorStartsStopped) {
  AudioPipeline pipeline;

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, DefaultGainIsZero) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, EndpointNameEmptyBeforeInitialize) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  EXPECT_TRUE(pipeline.endpoint_name().empty());
}

TEST(AudioPipelineTest, MaxBoostSetsMaxGain) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, FlatBoostSetsZeroGain) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(0.0);

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, HalfBoostScalesGainBySqrt) {
  constexpr double kHalfLevel = 0.5;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kHalfLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kHalfLevel), 1e-9);
}

TEST(AudioPipelineTest, BoostCurveIsConvexAtMidpoint) {
  constexpr double kHalfLevel = 0.5;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kHalfLevel);

  EXPECT_GT(pipeline.gain_db(), BassBoostFilter::kMaxGainDb * kHalfLevel);
}

TEST(AudioPipelineTest, BoostCurveIsConvexAtQuarterLevel) {
  constexpr double kQuarterLevel = 0.25;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kQuarterLevel);

  EXPECT_GT(pipeline.gain_db(), BassBoostFilter::kMaxGainDb * kQuarterLevel);
}

TEST(AudioPipelineTest, BoostLevelClampedAboveOne) {
  constexpr double kAboveMaxLevel = 2.0;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kAboveMaxLevel);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, BoostLevelClampedBelowZero) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(-1.0);

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, StopBeforeStartIsSafe) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, StopStreams()).Times(0);
  // `Close()` is called once by `Stop()` and once by the destructor.
  EXPECT_CALL(*device, Close()).Times(2);
  AudioPipeline pipeline(std::move(device));

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, RepeatedBoostLevelUpdatesReflectLatest) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(0.0);
  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtThreeQuarterLevel) {
  constexpr double kThreeQuarterLevel = 0.75;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kThreeQuarterLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kThreeQuarterLevel),
              1e-9);
}

TEST(AudioPipelineTest, DoubleStopIsSafe) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.Stop();
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, DestructorAfterStopIsSafe) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());
  pipeline.Stop();

  SUCCEED();
}

TEST(AudioPipelineTest, SetBoostLevelAfterStopStillUpdatesGain) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.Stop();
  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtTenPercentLevel) {
  constexpr double kTenPercentLevel = 0.1;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kTenPercentLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kTenPercentLevel), 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtNinetyPercentLevel) {
  constexpr double kNinetyPercentLevel = 0.9;
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

  pipeline.SetBoostLevel(kNinetyPercentLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kNinetyPercentLevel),
              1e-9);
}

TEST(AudioPipelineTest, StartFailsWhenDeviceOpenFails) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(
          AudioPipelineInterface::Status::Error(E_FAIL, L"Test open error")));
  AudioPipeline pipeline(std::move(device));

  const AudioPipelineInterface::Status status = pipeline.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  EXPECT_EQ(status.error_message, L"Test open error");
  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, StartSucceedsWithInjectedDevice) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  // `Start()` always initializes `filter_` from `device.sample_rate()` before
  // it reports success. This test does not assert on DSP behavior, so a normal
  // 48 kHz rate keeps the setup realistic without adding extra expectations.
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Configured Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  EXPECT_CALL(*device, ReadNextPacket())
      .WillRepeatedly(Return(CapturePacket{}));
  AudioPipeline pipeline(std::move(device));

  const AudioPipelineInterface::Status status = pipeline.Start();

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(pipeline.is_running());
  EXPECT_EQ(pipeline.endpoint_name(), L"Configured Test Device");
}

TEST(AudioPipelineTest, StartStreamsFailureStopsPipeline) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(
          AudioPipelineInterface::Status::Error(E_FAIL, L"Test start error")));
  AudioPipeline pipeline(std::move(device));
  ASSERT_TRUE(pipeline.Start().ok());

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, StartWhileRunningReturnsOk) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  EXPECT_CALL(*device, ReadNextPacket())
      .WillRepeatedly(Return(CapturePacket{}));
  AudioPipeline pipeline(std::move(device));
  ASSERT_TRUE(pipeline.Start().ok());

  const AudioPipelineInterface::Status second_start = pipeline.Start();

  EXPECT_TRUE(second_start.ok());
  EXPECT_TRUE(pipeline.is_running());
}

TEST(AudioPipelineTest, BoostLevelUpdatesWhileRunning) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  EXPECT_CALL(*device, ReadNextPacket())
      .WillRepeatedly(Return(CapturePacket{}));
  AudioPipeline pipeline(std::move(device));
  ASSERT_TRUE(pipeline.Start().ok());

  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, StopAfterStartCleansUpResources) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, StopStreams()).Times(1);
  // `Close()` is called once by `Stop()` and once by the destructor.
  EXPECT_CALL(*device, Close()).Times(2);
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Configured Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  EXPECT_CALL(*device, ReadNextPacket())
      .WillRepeatedly(Return(CapturePacket{}));
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_TRUE(pipeline.is_running());

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
  EXPECT_EQ(pipeline.endpoint_name(), L"Configured Test Device");
}

TEST(AudioPipelineTest, NonSilentPacketIsProcessedAndRendered) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> queue_drained;
  std::future<void> queue_drained_future = queue_drained.get_future();
  int render_call_count = 0;
  constexpr float kSampleA = 0.5F;
  constexpr float kSampleB = 0.3F;
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(CapturePacket{
          .samples = {kSampleA, -kSampleA, kSampleB, -kSampleB}, .frames = 2}))
      .WillOnce([&queue_drained] {
        queue_drained.set_value();
        return CapturePacket{};
      });
  EXPECT_CALL(*device, WriteRenderPacket(_))
      .WillOnce([&render_call_count](std::span<const float> /*pcm*/) {
        ++render_call_count;
        return S_OK;
      });
  AudioPipeline pipeline(std::move(device));
  pipeline.SetBoostLevel(1.0);

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(queue_drained_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();

  EXPECT_GT(render_call_count, 0);
}

TEST(AudioPipelineTest, SilentPacketIsNotRendered) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> queue_drained;
  std::future<void> queue_drained_future = queue_drained.get_future();
  constexpr float kSampleA = 0.5F;
  constexpr float kSampleB = 0.3F;
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(
          CapturePacket{.samples = {kSampleA, -kSampleA, kSampleB, -kSampleB},
                        .frames = 2,
                        .silent = true}))
      .WillOnce([&queue_drained] {
        queue_drained.set_value();
        return CapturePacket{};
      });
  EXPECT_CALL(*device, WriteRenderPacket(_)).Times(0);
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(queue_drained_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();
}

TEST(AudioPipelineTest, RenderedDeltaIsClamped) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> queue_drained;
  std::future<void> queue_drained_future = queue_drained.get_future();
  std::vector<float> rendered_samples;
  constexpr float kSampleA = 0.5F;
  constexpr float kSampleB = 0.3F;
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(CapturePacket{
          .samples = {kSampleA, -kSampleA, kSampleB, -kSampleB}, .frames = 2}))
      .WillOnce([&queue_drained] {
        queue_drained.set_value();
        return CapturePacket{};
      });
  EXPECT_CALL(*device, WriteRenderPacket(_))
      .WillOnce([&rendered_samples](std::span<const float> pcm) {
        rendered_samples.assign(pcm.begin(), pcm.end());
        return S_OK;
      });
  AudioPipeline pipeline(std::move(device));
  pipeline.SetBoostLevel(1.0);

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(queue_drained_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();

  EXPECT_THAT(rendered_samples, Each(AllOf(Ge(-1.0F), Le(1.0F))));
}

TEST(AudioPipelineTest, FailedReadWithoutRecoveryStopsPipeline) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> recover_attempted;
  std::future<void> recover_attempted_future = recover_attempted.get_future();
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(CapturePacket{.status = E_FAIL}));
  EXPECT_CALL(*device, TryRecover(E_FAIL))
      .WillOnce([&recover_attempted](HRESULT /*failure*/) {
        recover_attempted.set_value();
        return false;
      });
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(recover_attempted_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, FailedRenderWithoutRecoveryStopsPipeline) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate()).WillRepeatedly(Return(kSampleRateHz));
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> recover_attempted;
  std::future<void> recover_attempted_future = recover_attempted.get_future();
  constexpr float kSampleA = 0.5F;
  constexpr float kSampleB = 0.3F;
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(CapturePacket{
          .samples = {kSampleA, -kSampleA, kSampleB, -kSampleB}, .frames = 2}));
  EXPECT_CALL(*device, WriteRenderPacket(_)).WillOnce(Return(E_FAIL));
  EXPECT_CALL(*device, TryRecover(E_FAIL))
      .WillOnce([&recover_attempted](HRESULT /*failure*/) {
        recover_attempted.set_value();
        return false;
      });
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(recover_attempted_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, RecoverableFailureRefreshesFilterSampleRate) {
  auto device = std::make_unique<MockAudioDevice>();
  EXPECT_CALL(*device, Open())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  std::promise<void> sample_rate_refreshed;
  std::future<void> sample_rate_refreshed_future =
      sample_rate_refreshed.get_future();
  constexpr double kSampleRateHz = 48000.0;
  EXPECT_CALL(*device, sample_rate())
      .WillOnce(Return(kSampleRateHz))
      .WillOnce([&sample_rate_refreshed] {
        sample_rate_refreshed.set_value();
        constexpr double kNewSampleRateHz = 44100.0;
        return kNewSampleRateHz;
      });
  EXPECT_CALL(*device, endpoint_name())
      .WillRepeatedly(ReturnRefOfCopy(std::wstring(L"Test Device")));
  EXPECT_CALL(*device, StartStreams())
      .WillOnce(Return(AudioPipelineInterface::Status::Ok()));
  EXPECT_CALL(*device, ReadNextPacket())
      .WillOnce(Return(CapturePacket{.status = AUDCLNT_E_DEVICE_INVALIDATED}))
      .WillRepeatedly(Return(CapturePacket{}));
  EXPECT_CALL(*device, TryRecover(AUDCLNT_E_DEVICE_INVALIDATED))
      .WillOnce(Return(true));
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_EQ(sample_rate_refreshed_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

}  // namespace
