// Verifies the pipeline's initial state, boost level clamping, gain curve
// shape, and stop idempotency before any audio device has been opened.

#include "audio_pipeline.hpp"

#include <cmath>
#include <memory>

#include "bass_boost_filter.hpp"
#include "gtest/gtest.h"

namespace {

TEST(AudioPipelineTest, NotRunningBeforeStart) {
  AudioPipeline pipeline;

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, DefaultGainIsZero) {
  AudioPipeline pipeline;

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, EndpointNameEmptyBeforeInitialize) {
  AudioPipeline pipeline;

  EXPECT_TRUE(pipeline.endpoint_name().empty());
}

TEST(AudioPipelineTest, MaxBoostSetsMaxGain) {
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, FlatBoostSetsZeroGain) {
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(0.0);

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, HalfBoostScalesGainBySqrt) {
  constexpr double kHalfLevel = 0.5;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kHalfLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kHalfLevel), 1e-9);
}

TEST(AudioPipelineTest, BoostCurveIsConvexAtMidpoint) {
  // sqrt(0.5) ~= 0.707, more than the linear 0.5: audible boost well before
  // max.
  constexpr double kHalfLevel = 0.5;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kHalfLevel);

  EXPECT_GT(pipeline.gain_db(), BassBoostFilter::kMaxGainDb * kHalfLevel);
}

TEST(AudioPipelineTest, BoostCurveIsConvexAtQuarterLevel) {
  // sqrt(0.25) = 0.5, more than the linear 0.25.
  constexpr double kQuarterLevel = 0.25;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kQuarterLevel);

  EXPECT_GT(pipeline.gain_db(), BassBoostFilter::kMaxGainDb * kQuarterLevel);
}

TEST(AudioPipelineTest, BoostLevelClampedAboveOne) {
  constexpr double kAboveMaxLevel = 2.0;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kAboveMaxLevel);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, BoostLevelClampedBelowZero) {
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(-1.0);

  EXPECT_NEAR(pipeline.gain_db(), 0.0, 1e-9);
}

TEST(AudioPipelineTest, StopBeforeStartIsSafe) {
  AudioPipeline pipeline;

  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

// SetBoostLevel multiple times should always reflect the latest value.
TEST(AudioPipelineTest, RepeatedBoostLevelUpdatesReflectLatest) {
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(0.0);
  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtThreeQuarterLevel) {
  constexpr double kThreeQuarterLevel = 0.75;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kThreeQuarterLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kThreeQuarterLevel),
              1e-9);
}

TEST(AudioPipelineTest, DoubleStopIsSafe) {
  AudioPipeline pipeline;

  pipeline.Stop();
  pipeline.Stop();

  EXPECT_FALSE(pipeline.is_running());
}

TEST(AudioPipelineTest, DestructorAfterStopIsSafe) {
  auto pipeline = std::make_unique<AudioPipeline>();
  pipeline->Stop();
  pipeline.reset();

  // If we reach here, the destructor did not crash after explicit Stop.
  SUCCEED();
}

TEST(AudioPipelineTest, SetBoostLevelAfterStopStillUpdatesGain) {
  AudioPipeline pipeline;

  pipeline.Stop();
  pipeline.SetBoostLevel(1.0);

  EXPECT_NEAR(pipeline.gain_db(), BassBoostFilter::kMaxGainDb, 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtTenPercentLevel) {
  constexpr double kTenPercentLevel = 0.1;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kTenPercentLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kTenPercentLevel), 1e-9);
}

TEST(AudioPipelineTest, GainFollowsSqrtAtNinetyPercentLevel) {
  constexpr double kNinetyPercentLevel = 0.9;
  AudioPipeline pipeline;

  pipeline.SetBoostLevel(kNinetyPercentLevel);

  EXPECT_NEAR(pipeline.gain_db(),
              BassBoostFilter::kMaxGainDb * std::sqrt(kNinetyPercentLevel),
              1e-9);
}

}  // namespace
