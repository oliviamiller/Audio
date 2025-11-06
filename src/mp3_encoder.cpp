#include <cstring>
#include <stdexcept>
#include "mp3_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace microphone {

// Helper function to de-interleave audio samples
// Input: interleaved samples [L0, R0, L1, R1, ...]
// Output: planar format - plane[0]: [L0, L1, ...], plane[1]: [R0, R1, ...]
static void deinterleave_samples(const int16_t* interleaved,
                                  AVFrame* frame,
                                  int frame_size,
                                  int num_channels) {
    for (int ch = 0; ch < num_channels; ch++) {
        int16_t* plane = reinterpret_cast<int16_t*>(frame->data[ch]);
        for (int i = 0; i < frame_size; i++) {
            plane[i] = interleaved[i * num_channels + ch];
        }
    }
}

void initialize_mp3_encoder(MP3EncoderState& state, int sample_rate, int num_channels) {
    // Find MP3 encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!codec) {
        throw std::runtime_error("MP3 encoder not found");
    }

    CleanupPtr<avcodec_context_cleanup> encoder_ctx(avcodec_alloc_context3(codec));
    if (!encoder_ctx) {
        throw std::runtime_error("Could not allocate MP3 encoder context");
    }
    state.encoder_ctx = std::move(encoder_ctx);

    // Configure encoder
    state.encoder_ctx->sample_rate = sample_rate;
    av_channel_layout_default(&state.encoder_ctx->ch_layout, num_channels);
    // Planar 16-bit - explicitly supported format
    state.encoder_ctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    state.encoder_ctx->bit_rate = 192000;
    state.encoder_ctx->frame_size = 1152;  // Standard MP3 frame size

    if (avcodec_open2(state.encoder_ctx.get(), codec, nullptr) < 0) {
        throw std::runtime_error("Could not open MP3 encoder");
    }

    // Allocate reusable frame
    CleanupPtr<avframe_cleanup> frame(av_frame_alloc());
    if (!frame) {
        throw std::runtime_error("Could not allocate MP3 frame");
    }
    state.frame = std::move(frame);

    state.frame->nb_samples = state.encoder_ctx->frame_size;
    state.frame->format = state.encoder_ctx->sample_fmt;
    av_channel_layout_copy(&state.frame->ch_layout, &state.encoder_ctx->ch_layout);

    if (av_frame_get_buffer(state.frame.get(), 0) < 0) {
        throw std::runtime_error("Could not allocate MP3 frame buffer");
    }

    state.sample_rate = sample_rate;
    state.num_channels = num_channels;
    state.buffer.clear();

    VIAM_SDK_LOG(info) << "MP3 encoder initialized: " << sample_rate
                       << "Hz, " << num_channels << " channels, "
                       << state.encoder_ctx->frame_size << " samples/frame";
}

void encode_mp3_samples(MP3EncoderState& state,
                        const int16_t* samples,
                        int sample_count,
                        std::vector<uint8_t>& output_data) {
    if (!state.encoder_ctx || !state.frame) {
        throw std::runtime_error("MP3 encoder not initialized");
    }

    // Add new samples to buffer
    state.buffer.insert(state.buffer.end(), samples, samples + sample_count);

    size_t samples_per_frame = static_cast<size_t>(state.encoder_ctx->frame_size * state.num_channels);

    // Encode as many complete frames as we have buffered
    while (state.buffer.size() >= samples_per_frame) {
        // De-interleave samples into planar format
        deinterleave_samples(state.buffer.data(), state.frame.get(),
                           state.encoder_ctx->frame_size, state.num_channels);

        // Send frame to encoder
        int ret = avcodec_send_frame(state.encoder_ctx.get(), state.frame.get());
        if (ret < 0) {
            throw std::runtime_error("Error sending frame to MP3 encoder");
        }

        // Remove encoded samples from buffer
        state.buffer.erase(state.buffer.begin(), state.buffer.begin() + samples_per_frame);

        // Receive encoded packets (may get multiple or none due to encoder buffering)
        CleanupPtr<avpacket_cleanup> pkt(av_packet_alloc());
        if (!pkt) {
            throw std::runtime_error("Could not allocate packet");
        }

        while ((ret = avcodec_receive_packet(state.encoder_ctx.get(), pkt.get())) == 0) {
            output_data.insert(output_data.end(), pkt->data, pkt->data + pkt->size);
            av_packet_unref(pkt.get());
        }

        // AVERROR(EAGAIN) is expected when encoder needs more frames
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            VIAM_SDK_LOG(warn) << "MP3 encoder error: " << ret;
        }
    }
}

int flush_mp3_encoder(MP3EncoderState& state) {
    if (!state.encoder_ctx) {
        return 0;
    }

    // Flush encoder by sending NULL frame
    avcodec_send_frame(state.encoder_ctx.get(), nullptr);

    // Drain and count remaining packets
    CleanupPtr<avpacket_cleanup> pkt(av_packet_alloc());
    if (!pkt) {
        return 0;
    }

    int flushed_packets = 0;
    while (avcodec_receive_packet(state.encoder_ctx.get(), pkt.get()) == 0) {
        flushed_packets++;
        av_packet_unref(pkt.get());
    }

    if (flushed_packets > 0) {
        VIAM_SDK_LOG(info) << "MP3 encoder flushed " << flushed_packets << " remaining packets";
    }

    if (!state.buffer.empty()) {
        VIAM_SDK_LOG(info) << "Discarded " << state.buffer.size() / state.num_channels
                           << " unbuffered samples at end of stream";
    }

    return flushed_packets;
}

void cleanup_mp3_encoder(MP3EncoderState& state) {
    // Reset smart pointers
    state.frame.reset();
    state.encoder_ctx.reset();

    state.buffer.clear();
    state.sample_rate = 0;
    state.num_channels = 0;
}

} // namespace microphone
