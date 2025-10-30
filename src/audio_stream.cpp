#include "audio_stream.hpp"
#include "portaudio.h"
#include <algorithm>
#include <cstring>

namespace microphone {


AudioStreamContext::AudioStreamContext(const vsdk::audio_info& audio_info,
                                       size_t samples_per_chunk,
                                       size_t history_capacity)
    : history_write_index(0)
    , history_capacity(history_capacity)
    , info(audio_info)
    , samples_per_chunk(samples_per_chunk)
    , current_sample_count(0)
    , current_chunk_start_ns(0)
    , first_callback_adc_time(0.0)
    , first_callback_captured(false)
    , is_recording(true)
{
    // Pre-allocate working buffer for all channels (interleaved)
    working_buffer.resize(samples_per_chunk * audio_info.num_channels);
    history_buffer.resize(history_capacity);  // Pre-allocate history buffer
    // Note: stream_start_time will be set at first callback to anchor PA time to wall-clock time
}

std::vector<vsdk::AudioIn::audio_chunk> AudioStreamContext::get_new_chunks() {
    std::vector<vsdk::AudioIn::audio_chunk> result;

    // Pop all available chunks from lock-free queue
    vsdk::AudioIn::audio_chunk chunk;
    while (lockfree_queue.pop(chunk)) {
        // Add to history buffer for timestamp queries (with mutex, but not in RT thread)
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            history_buffer[history_write_index] = chunk;
            history_write_index = (history_write_index + 1) % history_capacity;
        }

        // Add to result
        result.push_back(std::move(chunk));
    }

    return result;
}

std::vector<vsdk::AudioIn::audio_chunk> AudioStreamContext::get_chunks_from_timestamp(
    int64_t start_timestamp_ns,
    int64_t end_timestamp_ns) {

    std::lock_guard<std::mutex> lock(history_mutex);
    std::vector<vsdk::AudioIn::audio_chunk> result;

    // Iterate through history buffer
    for (size_t i = 0; i < history_capacity; i++) {
        const auto& chunk = history_buffer[i];

        // Skip uninitialized chunks (timestamp == 0 means empty)
        if (chunk.start_timestamp_ns.count() == 0) {
            continue;
        }

        // Check if chunk is in requested time range
        if (chunk.start_timestamp_ns.count() >= start_timestamp_ns &&
            chunk.start_timestamp_ns.count() < end_timestamp_ns) {
            result.push_back(chunk);  // Copy chunk
        }
    }

    return result;
}

std::pair<int64_t, int64_t> AudioStreamContext::get_available_time_range() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(history_mutex));

    int64_t oldest = INT64_MAX;
    int64_t newest = 0;

    for (size_t i = 0; i < history_capacity; i++) {
        const auto& chunk = history_buffer[i];

        // Skip uninitialized chunks (timestamp == 0 means empty)
        if (chunk.start_timestamp_ns.count() == 0) {
            continue;
        }

        oldest = std::min(oldest, chunk.start_timestamp_ns.count());
        newest = std::max(newest, chunk.end_timestamp_ns.count());
    }

    if (oldest == INT64_MAX) {
        return {0, 0};  // No data
    }

    return {oldest, newest};
}


/**
 * PortAudio callback function - runs on real-time audio thread.
 *
 * for realtime applications, this function should avoid:
 * - Allocating memory (malloc/new)
 * - Accessing the file system
 * - Calling  any functions that may block
 * - Taking unpredictable amounts of time to complete
 */
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void *userData)
{
    AudioStreamContext* ctx = static_cast<AudioStreamContext*>(userData);
    // Handle null input or stopped recording
    if (inputBuffer == nullptr || !ctx->is_recording.load(std::memory_order_relaxed)) {
        return paContinue;
    }


    // If multiple channels, portaudio input buffer is interleaved.
    const int16_t* input = static_cast<const int16_t*>(inputBuffer);

    // First callback: establish anchor between PortAudio time and wall-clock time
    if (!ctx->first_callback_captured.load(std::memory_order_relaxed)) {
        // inputBufferAdcTime is the time when the first sample of the input buffer was captured,
        // in seconds and relative to a stream-specific clock
        ctx->first_callback_adc_time = timeInfo->inputBufferAdcTime;
        ctx->stream_start_time = std::chrono::system_clock::now();
        ctx->first_callback_captured.store(true, std::memory_order_relaxed);
    }

    // Calculate how many seconds elapsed since the first sample of the stream was captured.
    double seconds_since_stream_start = timeInfo->inputBufferAdcTime - ctx->first_callback_adc_time;

    // Process all frames from input buffer, creating and pushing chunks as they become full
    unsigned long i = 0;
    while (i < framesPerBuffer) {
        size_t buffer_idx = ctx->current_sample_count;

        // If starting a new chunk, capture its start timestamp
        if (buffer_idx == 0) {
            ctx->current_chunk_start_ns = calculate_sample_timestamp(ctx, seconds_since_stream_start, i);
        }

        // Get pointers to the beginning of the source frame and destination frame
        const int16_t* src_frame = &input[i * ctx->info.num_channels];
        int16_t* dst_frame = &ctx->working_buffer[buffer_idx * ctx->info.num_channels];

        // Copy all channels of this frame
        for (unsigned int ch = 0; ch < ctx->info.num_channels; ++ch) {
            dst_frame[ch] = src_frame[ch];
        }

        ++ctx->current_sample_count;
        ++i;

        // When we have a full chunk, create and queue it
        if (ctx->current_sample_count == ctx->samples_per_chunk) {
            vsdk::AudioIn::audio_chunk chunk;

            // Copy int16 PCM data directly from working buffer
            // Total samples = frames * channels (e.g., 100 frames * 2 channels = 200 samples)
            size_t total_samples = ctx->samples_per_chunk * ctx->info.num_channels;
            // Note: resize can allocate memory
            chunk.audio_data.resize(total_samples * sizeof(int16_t));
            int16_t* audio_samples = reinterpret_cast<int16_t*>(chunk.audio_data.data());

            for (size_t j = 0; j < total_samples; j++) {
                audio_samples[j] = ctx->working_buffer[j];
            }

            chunk.info = ctx->info;
            chunk.start_timestamp_ns = ctx->current_chunk_start_ns;
            chunk.end_timestamp_ns = ctx->current_chunk_start_ns +
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::duration<double>(ctx->samples_per_chunk /
                        static_cast<double>(ctx->info.sample_rate_hz))
                );

            ctx->push_chunk(std::move(chunk));

            // Reset for next chunk
            ctx->current_sample_count = 0;
        }
    }

    return paContinue;
}

std::chrono::nanoseconds calculate_sample_timestamp(
    const AudioStreamContext* ctx,
    double seconds_since_stream_start,
    unsigned long sample_index)
{
    double seconds_per_sample = 1.0 / static_cast<double>(ctx->info.sample_rate_hz);
    double sample_elapsed_seconds = seconds_since_stream_start + (sample_index * seconds_per_sample);

    auto sample_elapsed_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(sample_elapsed_seconds)
    );

    auto sample_absolute_time = ctx->stream_start_time + sample_elapsed_duration;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        sample_absolute_time.time_since_epoch()
    );
}

} // namespace microphone
