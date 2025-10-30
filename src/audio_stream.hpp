#pragma once

#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/common/audio.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include "portaudio.h"
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>

namespace microphone {

namespace vsdk = ::viam::sdk;

/**
 * Lock-free audio buffer for real-time audio streaming.
 *
 * Uses a single-producer, single-consumer lock-free queue to safely transfer
 * audio chunks from the real-time PortAudio callback thread to the consumer thread.
 *
 * Thread Safety:
 * - push_chunk(): Called from RT audio callback thread (lock-free, wait-free)
 * - get_new_chunks(): Called from consumer thread
 * - get_chunks_from_timestamp(): Called from consumer thread (uses mutex)
 */
struct AudioStreamContext {
    // Lock-free queue for real-time audio callback (single producer, single consumer)
    // Capacity of 100 chunks (e.g., 10 seconds at 100ms chunks)
    boost::lockfree::spsc_queue<vsdk::AudioIn::audio_chunk, boost::lockfree::capacity<100>> lockfree_queue;

    // Separate history buffer for timestamp-based queries (protected by mutex, only accessed from consumer thread)
    std::vector<vsdk::AudioIn::audio_chunk> history_buffer;
    size_t history_write_index;
    size_t history_capacity;
    std::mutex history_mutex;  // Only used in consumer thread, NOT in callback
    vsdk::audio_info info; // avoid reconstruction for each audio chunk

    // Pre-allocated working buffer for the callback
    // Temp storage for accumulating samples until we have a full chunk
    std::vector<int16_t> working_buffer;
    size_t samples_per_chunk;
    size_t current_sample_count;

    // Timing
    std::chrono::system_clock::time_point stream_start_time;  // Start time of stream in Unix time
    std::chrono::nanoseconds current_chunk_start_ns;  // Track start time of accumulating chunk
    double first_callback_adc_time;  // Baseline PortAudio time from first callback
    std::atomic<bool> first_callback_captured;  // Whether we've captured the baseline

    std::atomic<bool> is_recording;

    AudioStreamContext(const vsdk::audio_info& audio_info,
                      size_t samples_per_chunk,
                      size_t history_capacity = 100);

    /**
     * Lock-free push - called from real-time audio callback.
     * This is wait-free and safe for audiocallback - no blocking, no allocation.
     */
    inline void push_chunk(vsdk::AudioIn::audio_chunk&& chunk) {
        lockfree_queue.push(std::move(chunk));
    }

    /**
     * Consumer-side method: pop chunks from lock-free queue.
     * This should be called from get_audio() (non RT thread)
     */
    std::vector<vsdk::AudioIn::audio_chunk> get_new_chunks();

    /**
     * Query chunks by timestamp range from history buffer.
     * Safe to call from any non-RT thread.
     */
    std::vector<vsdk::AudioIn::audio_chunk> get_chunks_from_timestamp(
        int64_t start_timestamp_ns,
        int64_t end_timestamp_ns = INT64_MAX);

    /**
     * Get the time range of available audio data in the history buffer.
     */
    std::pair<int64_t, int64_t> get_available_time_range() const;
};

/**
 * Calculate absolute wall-clock timestamp for a specific sample.
 *
 * @param ctx Audio stream context
 * @param seconds_since_stream_start Elapsed PA time since first callback (seconds)
 * @param sample_index Index of the sample within current callback buffer
 * @return Absolute timestamp in nanoseconds since Unix epoch
 */
std::chrono::nanoseconds calculate_sample_timestamp(
    const AudioStreamContext* ctx,
    double seconds_since_stream_start,
    unsigned long sample_index);

/**
 * PortAudio callback function - runs on real-time audio thread.
 *
 * CRITICAL: This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 * From PortAudio docs: Do not allocate memory, access the file system,
 * call library functions or call other functions from the stream callback
 * that may block or take an unpredictable amount of time to complete.
 */
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void *userData);

} // namespace microphone
