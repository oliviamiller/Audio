#pragma once

#include <viam/sdk/components/audio_out.hpp>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "portaudio.h"
#include "portaudio.hpp"
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

namespace speaker {
namespace vsdk = ::viam::sdk;

struct SpeakerStreamConfig {
    PaDeviceIndex device_index;
    int channels;
    int sample_rate;
    double latency = 0.0;
    PaStreamCallback* callback = nullptr;
    void* user_data = nullptr;
};

struct SpeakerConfigParams {
    std::string device_name;
    std::optional<int> sample_rate;
    std::optional<int> num_channels;
    std::optional<double> latency_ms;
};

SpeakerConfigParams parseSpeakerConfigAttributes(const viam::sdk::ResourceConfig& cfg);

void openSpeakerStream(PaStream** stream,
                       const SpeakerStreamConfig& config,
                       audio::portaudio::PortAudioInterface* pa = nullptr);
void startSpeakerStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa = nullptr);
PaDeviceIndex findSpeakerDeviceByName(const std::string& name, audio::portaudio::PortAudioInterface* pa = nullptr);
void shutdownSpeakerStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa = nullptr);


class Speaker final : public viam::sdk::AudioOut, public viam::sdk::Reconfigurable {
public:
    Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
            audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Speaker();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

    void play(std::vector<uint8_t> const& audio_data,
                      boost::optional<viam::sdk::audio_info> info,
                      const viam::sdk::ProtoStruct& extra);

    viam::sdk::audio_properties get_properties(const viam::sdk::ProtoStruct& extra);
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra);
    void reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);
    void setupStreamFromConfig(const SpeakerConfigParams& params);

    // Member variables
    std::string device_name_;
    double latency_;
    static vsdk::Model model;

    // The mutex protects the stream and playback buffer
    std::mutex stream_mu_;
    PaStream* stream_;
    audio::portaudio::PortAudioInterface* pa_;

};

} // namespace speaker
