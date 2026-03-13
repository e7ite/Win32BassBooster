// Verifies the pipeline's initial state, boost level clamping, gain curve
// shape, and start/stop lifecycle through an injected test audio device.

#include "audio_pipeline.hpp"

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "audio_device.hpp"
#include "bass_boost_filter.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

class MockAudioDevice final : public AudioDevice {
 public:
  void SetOpenStatus(AudioPipelineInterface::Status status) {
    open_status_ = std::move(status);
  }

  void SetStartStreamsStatus(AudioPipelineInterface::Status status) {
    start_streams_status_ = std::move(status);
  }

  void SetSampleRateHz(double sample_rate_hz) {
    sample_rate_hz_ = sample_rate_hz;
  }

  void SetEndpointName(std::wstring endpoint_name) {
    endpoint_name_ = std::move(endpoint_name);
  }

  // Queues a packet to be returned by `ReadNextPacket`. Must be called before
  // `Start()` to avoid racing with the audio thread.
  void EnqueuePacket(CapturePacket packet) {
    packets_.push_back(std::move(packet));
  }

  AudioPipelineInterface::Status Open() override {
    opened_ = open_status_.ok();
    return open_status_;
  }

  AudioPipelineInterface::Status StartStreams() override {
    return start_streams_status_;
  }

  void StopStreams() override {}

  void Close() override { opened_ = false; }

  // Returns queued packets in order, then empty packets once the queue is
  // exhausted. Only called from the audio thread after `Start()`.
  CapturePacket ReadNextPacket() override {
    if (read_index_ >= packets_.size()) {
      packets_drained_.store(true);
      packets_drained_.notify_one();
      return {};
    }
    return std::move(packets_[read_index_++]);
  }

  // Blocks until the audio thread has consumed all queued packets and hit the
  // first empty read. Call after `Start()` and before `Stop()` to ensure the
  // DSP path ran.
  void WaitForPacketsDrained() { packets_drained_.wait(false); }

  // Captures the last rendered samples so tests can verify the DSP output
  // after `Stop()` joins the audio thread.
  HRESULT WriteRenderPacket(std::span<const float> pcm) override {
    rendered_samples_.assign(pcm.begin(), pcm.end());
    ++render_call_count_;
    return S_OK;
  }

  bool TryRecover(HRESULT /*failure*/) override { return false; }

  double sample_rate() const override { return sample_rate_hz_; }

  const std::wstring& endpoint_name() const override { return endpoint_name_; }

  [[nodiscard]] bool opened() const { return opened_; }
  [[nodiscard]] int render_call_count() const { return render_call_count_; }
  [[nodiscard]] const std::vector<float>& rendered_samples() const {
    return rendered_samples_;
  }

 private:
  AudioPipelineInterface::Status open_status_;
  AudioPipelineInterface::Status start_streams_status_;
  double sample_rate_hz_ = 48000.0;
  std::wstring endpoint_name_ = L"Test Device";
  bool opened_ = false;

  std::vector<CapturePacket> packets_;
  size_t read_index_ = 0;
  std::atomic<bool> packets_drained_ = false;
  std::vector<float> rendered_samples_;
  int render_call_count_ = 0;
};

TEST(AudioPipelineTest, NotRunningBeforeStart) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

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
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());

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
  device->SetOpenStatus(
      AudioPipelineInterface::Status::Error(E_FAIL, L"Test open error"));
  AudioPipeline pipeline(std::move(device));

  const AudioPipelineInterface::Status status = pipeline.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, E_FAIL);
  EXPECT_EQ(status.error_message, L"Test open error");
  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, StartSucceedsWithInjectedDevice) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  device->SetEndpointName(L"Configured Test Device");
  AudioPipeline pipeline(std::move(device));

  const AudioPipelineInterface::Status status = pipeline.Start();

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(pipeline.is_running());
  EXPECT_TRUE(device_ref.opened());
  EXPECT_EQ(pipeline.endpoint_name(), L"Configured Test Device");
}

TEST(AudioPipelineTest, StartStreamsFailureStopsPipeline) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  device->SetStartStreamsStatus(
      AudioPipelineInterface::Status::Error(E_FAIL, L"Test start error"));
  AudioPipeline pipeline(std::move(device));
  ASSERT_TRUE(pipeline.Start().ok());

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
  EXPECT_FALSE(device_ref.opened());
}

TEST(AudioPipelineTest, StartWhileRunningReturnsOk) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());
  ASSERT_TRUE(pipeline.Start().ok());

  const AudioPipelineInterface::Status second_start = pipeline.Start();

  EXPECT_TRUE(second_start.ok());
  EXPECT_TRUE(pipeline.is_running());
}

TEST(AudioPipelineTest, BoostLevelUpdatesWhileRunning) {
  AudioPipeline pipeline(std::make_unique<MockAudioDevice>());
  ASSERT_TRUE(pipeline.Start().ok());

  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, StopAfterStartCleansUpResources) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  device->SetEndpointName(L"Configured Test Device");
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  ASSERT_TRUE(pipeline.is_running());

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
  EXPECT_FALSE(device_ref.opened());
  EXPECT_EQ(pipeline.endpoint_name(), L"Configured Test Device");
}

TEST(AudioPipelineTest, NonSilentPacketIsProcessedAndRendered) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  CapturePacket packet;
  packet.frames = 2;
  packet.samples = {0.5F, -0.5F, 0.3F, -0.3F};
  device->EnqueuePacket(std::move(packet));
  AudioPipeline pipeline(std::move(device));
  pipeline.SetBoostLevel(1.0);

  ASSERT_TRUE(pipeline.Start().ok());
  device_ref.WaitForPacketsDrained();
  pipeline.Stop();

  EXPECT_GT(device_ref.render_call_count(), 0);
}

TEST(AudioPipelineTest, SilentPacketIsNotRendered) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  CapturePacket packet;
  packet.frames = 2;
  packet.silent = true;
  packet.samples = {0.5F, -0.5F, 0.3F, -0.3F};
  device->EnqueuePacket(std::move(packet));
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  device_ref.WaitForPacketsDrained();
  pipeline.Stop();

  EXPECT_EQ(device_ref.render_call_count(), 0);
}

TEST(AudioPipelineTest, RenderedDeltaIsClamped) {
  auto device = std::make_unique<MockAudioDevice>();
  MockAudioDevice& device_ref = *device;
  CapturePacket packet;
  packet.frames = 2;
  packet.samples = {0.5F, -0.5F, 0.3F, -0.3F};
  device->EnqueuePacket(std::move(packet));
  AudioPipeline pipeline(std::move(device));
  pipeline.SetBoostLevel(1.0);

  ASSERT_TRUE(pipeline.Start().ok());
  device_ref.WaitForPacketsDrained();
  pipeline.Stop();

  EXPECT_THAT(device_ref.rendered_samples(),
              ::testing::Each(
                  ::testing::AllOf(::testing::Ge(-1.0F), ::testing::Le(1.0F))));
}

TEST(AudioPipelineTest, FailedReadWithoutRecoveryStopsPipeline) {
  auto device = std::make_unique<MockAudioDevice>();
  CapturePacket failed_packet;
  failed_packet.status = E_FAIL;
  device->EnqueuePacket(std::move(failed_packet));
  AudioPipeline pipeline(std::move(device));

  ASSERT_TRUE(pipeline.Start().ok());
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

}  // namespace
