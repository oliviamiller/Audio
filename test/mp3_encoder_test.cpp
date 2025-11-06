#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "mp3_encoder.hpp"

using namespace microphone;

// Initialize Viam SDK instance for logging support
class MP3EncoderTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        instance_ = std::make_unique<viam::sdk::Instance>();
    }
    void TearDown() override {
        instance_.reset();
    }
private:
    std::unique_ptr<viam::sdk::Instance> instance_;
};
class MP3EncoderTest : public ::testing::Test {
protected:
    MP3EncoderState state_;

    void TearDown() override {
        cleanup_mp3_encoder(state_);
    }

    // Helper to create test audio samples (simple sine-like wave)
    std::vector<int16_t> create_test_samples(int num_samples) {
        std::vector<int16_t> samples(num_samples);
        for (int i = 0; i < num_samples; i++) {
            samples[i] = static_cast<int16_t>(10000.0 * std::sin(2.0 * M_PI * i / 100.0));
        }
        return samples;
    }
};

TEST_F(MP3EncoderTest, InitializeSucceeds) {
    ASSERT_NO_THROW(initialize_mp3_encoder(state_, 48000, 2));

    EXPECT_NE(state_.encoder_ctx, nullptr);
    EXPECT_NE(state_.frame, nullptr);
    EXPECT_EQ(state_.sample_rate, 48000);
    EXPECT_EQ(state_.num_channels, 2);
    EXPECT_TRUE(state_.buffer.empty());
}

// Test encoding small amount of samples (less than one MP3 frame)
TEST_F(MP3EncoderTest, EncodeIncompleteMp3Frame) {
    initialize_mp3_encoder(state_, 48000, 2);

    // This is less than 1152 samples needed for one MP3 frame
    auto samples = create_test_samples(500 * 2);
    std::vector<uint8_t> output;

    encode_mp3_samples(state_, samples.data(), samples.size(), output);

    // Should buffer the samples but not produce output yet
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(state_.buffer.size(), samples.size());
}

// Test encoding exactly one MP3 frame
TEST_F(MP3EncoderTest, EncodeOneCompleteMp3Frame) {
    initialize_mp3_encoder(state_, 48000, 2);

    // Create exactly 1152 frames * 2 channels = 2304 samples
    auto samples = create_test_samples(1152 * 2);
    std::vector<uint8_t> output;

    encode_mp3_samples(state_, samples.data(), samples.size(), output);

    // Buffer should be empty (all samples consumed into one frame)
    EXPECT_TRUE(state_.buffer.empty());

    // MP3 encoder may not output yet due to lookahead buffering
    // Send more frames to force output
    auto more_samples = create_test_samples(1152 * 4 * 2);
    encode_mp3_samples(state_, more_samples.data(), more_samples.size(), output);
    EXPECT_FALSE(output.empty()) << "Should have MP3 output after multiple frames";
}
TEST_F(MP3EncoderTest, EncodeMultipleMp3Frames) {
    initialize_mp3_encoder(state_, 48000, 2);

    // Create 3.5 frames worth of data
    // 1152 * 3.5 = 4032 frames * 2 channels = 8064 samples
    auto samples = create_test_samples(4032 * 2);
    std::vector<uint8_t> output;

    encode_mp3_samples(state_, samples.data(), samples.size(), output);
    EXPECT_FALSE(output.empty());
    // Buffer should contain the leftover 0.5 frame
    EXPECT_EQ(state_.buffer.size(), 576 * 2);
}

TEST_F(MP3EncoderTest, AccumulateAcrossMultipleCalls) {
    initialize_mp3_encoder(state_, 48000, 2);

    std::vector<uint8_t> output;

    // First call: 500 samples (not enough for a frame)
    auto samples1 = create_test_samples(500 * 2);
    encode_mp3_samples(state_, samples1.data(), samples1.size(), output);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(state_.buffer.size(), 500 * 2);

    // Second call: 700 more samples (total = 1200, enough for 1 frame)
    auto samples2 = create_test_samples(700 * 2);
    encode_mp3_samples(state_, samples2.data(), samples2.size(), output);

    // MP3 encoder uses lookahead buffering - may not output immediately
    // Just verify buffer state is correct (1200 - 1152 = 48 samples left)
    EXPECT_EQ(state_.buffer.size(), 48 * 2);

    // Send more frames to force output from lookahead buffer
    auto samples3 = create_test_samples(1152 * 5 * 2);  // 5 more frames
    encode_mp3_samples(state_, samples3.data(), samples3.size(), output);
    EXPECT_FALSE(output.empty()) << "Should have MP3 output after sending multiple frames";
}

TEST_F(MP3EncoderTest, FlushEncoder) {
    initialize_mp3_encoder(state_, 48000, 2);

    auto samples = create_test_samples(1152 * 5 * 2);
    std::vector<uint8_t> output;
    encode_mp3_samples(state_, samples.data(), samples.size(), output);

    // Flush should retrieve any buffered packets from the encoder
    int flushed = flush_mp3_encoder(state_);
    EXPECT_GT(flushed, 0);
}

TEST_F(MP3EncoderTest, CleanupEncoder) {
    initialize_mp3_encoder(state_, 48000, 2);

    EXPECT_NE(state_.encoder_ctx, nullptr);
    EXPECT_NE(state_.frame, nullptr);

    cleanup_mp3_encoder(state_);

    EXPECT_EQ(state_.encoder_ctx, nullptr);
    EXPECT_EQ(state_.frame, nullptr);
    EXPECT_EQ(state_.sample_rate, 0);
    EXPECT_EQ(state_.num_channels, 0);
    EXPECT_TRUE(state_.buffer.empty());
}

TEST_F(MP3EncoderTest, EncodeWithoutInitialization) {
    auto samples = create_test_samples(1152 * 2);
    std::vector<uint8_t> output;

    // Should throw because encoder is not initialized
    EXPECT_THROW(
        encode_mp3_samples(state_, samples.data(), samples.size(), output),
        std::runtime_error
    );
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MP3EncoderTestEnvironment);
  return RUN_ALL_TESTS();
}
