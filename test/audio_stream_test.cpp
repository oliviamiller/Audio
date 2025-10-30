#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include "audio_stream.hpp"

using namespace microphone;
using namespace viam::sdk;

class AudioStreamContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a basic audio context for testing
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};  // mono, 44.1kHz
        samples_per_chunk_ = 4410;  // 100ms chunks
        context_ = std::make_unique<AudioStreamContext>(info, samples_per_chunk_);
    }

    void TearDown() override {
        context_.reset();
    }

    // Helper to create a test chunk
    AudioIn::audio_chunk CreateTestChunk(int sequence, int64_t start_ns = 0) {
        AudioIn::audio_chunk chunk;
        chunk.start_timestamp_ns = std::chrono::nanoseconds(start_ns);
        chunk.end_timestamp_ns = std::chrono::nanoseconds(start_ns + 100000000); // +100ms
        chunk.info = context_->info;

        // Create some dummy audio data (100 samples of int16)
        chunk.audio_data.resize(100 * sizeof(int16_t));
        int16_t* samples = reinterpret_cast<int16_t*>(chunk.audio_data.data());
        for (int i = 0; i < 100; i++) {
            samples[i] = static_cast<int16_t>(i * 100 + sequence);
        }

        return chunk;
    }

    std::unique_ptr<AudioStreamContext> context_;
    size_t samples_per_chunk_;
};


TEST_F(AudioStreamContextTest, ConstructorInitializesCorrectly) {
    EXPECT_EQ(context_->samples_per_chunk, samples_per_chunk_);
    EXPECT_EQ(context_->current_sample_count, 0);
    EXPECT_TRUE(context_->is_recording.load());
    // Working buffer size = samples_per_chunk * num_channels (1 for mono)
    EXPECT_EQ(context_->working_buffer.size(), samples_per_chunk_ * context_->info.num_channels);
    EXPECT_EQ(context_->history_buffer.size(), 100);  // default capacity
}

TEST_F(AudioStreamContextTest, ConstructorWithCustomHistoryCapacity) {
    audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
    size_t custom_capacity = 50;

    AudioStreamContext ctx(info, 4410, custom_capacity);

    EXPECT_EQ(ctx.history_capacity, custom_capacity);
    EXPECT_EQ(ctx.history_buffer.size(), custom_capacity);
}

TEST_F(AudioStreamContextTest, StereoBufferSizeCorrect) {
    audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 2};  // stereo
    size_t samples_per_chunk = 4410;  // 100ms chunks

    AudioStreamContext ctx(info, samples_per_chunk);

    // Working buffer should account for 2 channels (interleaved)
    EXPECT_EQ(ctx.working_buffer.size(), samples_per_chunk * 2);
    EXPECT_EQ(ctx.info.num_channels, 2);
}


TEST_F(AudioStreamContextTest, PushChunkSucceeds) {
    auto chunk = CreateTestChunk(1);

    // Push should not throw
    EXPECT_NO_THROW(context_->push_chunk(std::move(chunk)));
}

TEST_F(AudioStreamContextTest, GetNewChunksReturnsEmpty) {
    auto chunks = context_->get_new_chunks();

    EXPECT_TRUE(chunks.empty());
}

TEST_F(AudioStreamContextTest, PushAndPopSingleChunk) {
    auto chunk = CreateTestChunk(1, 1000000);  // 1ms timestamp
    context_->push_chunk(std::move(chunk));

    auto chunks = context_->get_new_chunks();

    ASSERT_EQ(chunks.size(), 1);
    EXPECT_EQ(chunks[0].start_timestamp_ns.count(), 1000000);
}

TEST_F(AudioStreamContextTest, GetNewChunksEmptiesQueue) {
    // Push 3 chunks
    for (int i = 0; i < 3; i++) {
        context_->push_chunk(CreateTestChunk(i));
    }

    // First call gets all chunks
    auto chunks1 = context_->get_new_chunks();
    EXPECT_EQ(chunks1.size(), 3);

    // Second call should be empty
    auto chunks2 = context_->get_new_chunks();
    EXPECT_TRUE(chunks2.empty());
}


TEST_F(AudioStreamContextTest, HistoryBufferStoresChunks) {
    // Push a chunk
    auto chunk = CreateTestChunk(42, 5000000);
    context_->push_chunk(std::move(chunk));

    // Pop it (which adds to history)
    context_->get_new_chunks();

    // Query from history
    auto history_chunks = context_->get_chunks_from_timestamp(0, INT64_MAX);

    ASSERT_EQ(history_chunks.size(), 1);
}

TEST_F(AudioStreamContextTest, GetChunksFromTimestampFiltersCorrectly) {
    // Push chunks at different timestamps
    context_->push_chunk(CreateTestChunk(1, 1000000000));   // 1 second
    context_->push_chunk(CreateTestChunk(2, 2000000000));   // 2 seconds
    context_->push_chunk(CreateTestChunk(3, 3000000000));   // 3 seconds

    // Get all chunks into history
    context_->get_new_chunks();

    // Query for chunks between 1.5s and 2.5s
    auto chunks = context_->get_chunks_from_timestamp(1500000000, 2500000000);

    ASSERT_EQ(chunks.size(), 1);
}

TEST_F(AudioStreamContextTest, GetChunksFromTimestampWithDefaultEndTime) {
    context_->push_chunk(CreateTestChunk(1, 1000000000));
    context_->push_chunk(CreateTestChunk(2, 2000000000));

    context_->get_new_chunks();

    // Query from 1.5s to end (default INT64_MAX)
    auto chunks = context_->get_chunks_from_timestamp(1500000000);

    ASSERT_EQ(chunks.size(), 1);
}

TEST_F(AudioStreamContextTest, GetAvailableTimeRangeReturnsCorrectRange) {
    // Push chunks at different times
    context_->push_chunk(CreateTestChunk(1, 1000000000));   // Start: 1s
    context_->push_chunk(CreateTestChunk(2, 2000000000));   // Start: 2s
    context_->push_chunk(CreateTestChunk(3, 3000000000));   // Start: 3s, End: 3.1s

    context_->get_new_chunks();

    auto range = context_->get_available_time_range();

    EXPECT_EQ(range.first, 1000000000);      // Oldest start
    EXPECT_EQ(range.second, 3100000000);     // Newest end (3s + 100ms)
}

TEST_F(AudioStreamContextTest, GetAvailableTimeRangeEmptyReturnsZero) {
    auto range = context_->get_available_time_range();

    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.second, 0);
}


TEST_F(AudioStreamContextTest, HistoryBufferWrapsAround) {
    audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
    size_t small_capacity = 5;
    AudioStreamContext ctx(info, 4410, small_capacity);

    // Push more chunks than capacity
    for (int i = 0; i < 10; i++) {
        ctx.push_chunk(CreateTestChunk(i, i * 100000000));
    }

    ctx.get_new_chunks();

    // Should only have the last 5 chunks in history
    auto chunks = ctx.get_chunks_from_timestamp(0, INT64_MAX);

    ASSERT_EQ(chunks.size(), 5);
}

TEST_F(AudioStreamContextTest, ConcurrentPushAndPop) {
    std::atomic<bool> stop{false};
    std::atomic<int> pushed{0};
    std::atomic<int> popped{0};

    // Producer thread (simulates RT audio callback)
    std::thread producer([&]() {
        for (int i = 0; i < 100; i++) {
            context_->push_chunk(CreateTestChunk(i));
            pushed++;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Consumer thread (simulates get_audio)
    std::thread consumer([&]() {
        while (!stop.load() || pushed.load() > popped.load()) {
            auto chunks = context_->get_new_chunks();
            popped += chunks.size();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    producer.join();
    stop = true;
    consumer.join();

    // All chunks should have been consumed
    EXPECT_EQ(popped.load(), 100);
}

TEST_F(AudioStreamContextTest, RecordingFlagCanBeToggled) {
    EXPECT_TRUE(context_->is_recording.load());

    context_->is_recording.store(false);
    EXPECT_FALSE(context_->is_recording.load());

    context_->is_recording.store(true);
    EXPECT_TRUE(context_->is_recording.load());
}

TEST_F(AudioStreamContextTest, PushChunkAfterMoveStillWorks) {
    auto chunk1 = CreateTestChunk(1);
    auto chunk2 = CreateTestChunk(2);

    context_->push_chunk(std::move(chunk1));
    context_->push_chunk(std::move(chunk2));

    auto chunks = context_->get_new_chunks();
    EXPECT_EQ(chunks.size(), 2);
}

TEST_F(AudioStreamContextTest, EmptyAudioDataChunk) {
    AudioIn::audio_chunk chunk;
    chunk.audio_data.clear();  // Empty data

    context_->push_chunk(std::move(chunk));

    auto chunks = context_->get_new_chunks();
    ASSERT_EQ(chunks.size(), 1);
    EXPECT_TRUE(chunks[0].audio_data.empty());
}

TEST_F(AudioStreamContextTest, PushPop) {
    // Queue capacity is 100, so test with chunks that fit
    const int num_chunks = 90;  // Stay under capacity

    auto start = std::chrono::high_resolution_clock::now();

    // Push many chunks
    for (int i = 0; i < num_chunks; i++) {
        context_->push_chunk(CreateTestChunk(i));
    }

    // Pop all chunks
    int total_retrieved = 0;
    while (true) {
        auto chunks = context_->get_new_chunks();
        if (chunks.empty()) break;
        total_retrieved += chunks.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(total_retrieved, num_chunks);

    // Should complete in reasonable time (< 100ms on modern hardware)
    EXPECT_LT(duration.count(), 100) << "Performance test took " << duration.count() << "ms";
}

TEST_F(AudioStreamContextTest, QueueDropsChunksWhenFull) {
    // Queue capacity is 100 - test that pushing beyond capacity doesn't crash
    const int num_chunks = 150;  // Push more than capacity

    // Push chunks
    for (int i = 0; i < num_chunks; i++) {
        context_->push_chunk(CreateTestChunk(i));
    }

    // Pop all chunks - should get at most 100
    auto chunks = context_->get_new_chunks();

    EXPECT_LE(chunks.size(), 100) << "Queue should not hold more than capacity";
    EXPECT_GT(chunks.size(), 0) << "Should have retrieved some chunks";
}

TEST_F(AudioStreamContextTest, CalculateSampleTimestamp) {
    // Set up the baseline time
    context_->first_callback_adc_time = 1000.0;
    context_->stream_start_time = std::chrono::system_clock::now();
    context_->first_callback_captured.store(true);

    auto baseline_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        context_->stream_start_time.time_since_epoch()
    ).count();

    auto timestamp1 = calculate_sample_timestamp(context_.get(), 0.0, 0);
    EXPECT_EQ(timestamp1.count(), baseline_ns);

    auto timestamp2 = calculate_sample_timestamp(context_.get(), 1.0, 0);
    EXPECT_EQ(timestamp2.count(), baseline_ns + 1'000'000'000);

    // Test 3: Sample offset at 44100 Hz (1 sample = ~22.676 µs)
    auto timestamp3 = calculate_sample_timestamp(context_.get(), 0.0, 44100);
    EXPECT_NEAR(timestamp3.count(), baseline_ns + 1'000'000'000, 1000);  // ~1 second
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
