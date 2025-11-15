#include "speaker.hpp"
#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/registry/registry.hpp>
#include <viam/sdk/components/audio_out.hpp>
#include <cstring>
#include <stdexcept>

namespace speaker {
namespace vsdk = ::viam::sdk;


Speaker::Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
                       audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioOut(cfg.name()), pa_(pa), stream_(nullptr) {
        

}

Speaker::~Speaker() {
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
    }
}

vsdk::Model Speaker::model = {"viam", "audio", "speaker"};

/**
 * PortAudio callback function - runs on real-time audio thread.
 * This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 * Lock-free implementation using atomic circular buffer.
 */
int speakerCallback(const void* /*inputBuffer*/,
                     void* outputBuffer,
                     unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo* /*timeInfo*/,
                     PaStreamCallbackFlags /*statusFlags*/,
                     void* userData) {


}



std::vector<std::string> Speaker::validate(vsdk::ResourceConfig cfg) {
    auto attrs = cfg.attributes();

    if(attrs.count("device_name")) {
        if (!attrs["device_name"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_name attribute must be a string";
            throw std::invalid_argument("device_name attribute must be a string");
        }
    }

    if(attrs.count("latency")) {
        if (!attrs["latency"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] latency attribute must be a number";
            throw std::invalid_argument("latency attribute must be a number");
        }
        double latency_ms = *attrs.at("latency").get<double>();
        if (latency_ms < 0) {
            VIAM_SDK_LOG(error) << "[validate] latency must be non-negative";
            throw std::invalid_argument("latency must be non-negative");
        }
    }

    return {};
}

viam::sdk::ProtoStruct Speaker::do_command(const viam::sdk::ProtoStruct& command) {
    vsdk::ProtoStruct result;
    return result;
}


void Speaker::play(std::vector<uint8_t> const& audio_data,
                      boost::optional<viam::sdk::audio_info> info,
                      const viam::sdk::ProtoStruct& extra) {

}

viam::sdk::audio_properties Speaker::get_properties(const vsdk::ProtoStruct& extra) {

}

std::vector<viam::sdk::GeometryConfig> Speaker::get_geometries(const viam::sdk::ProtoStruct& extra) {
    return {};
}

void Speaker::reconfigure(const vsdk::Dependencies& deps,
                          const vsdk::ResourceConfig& cfg) {

}


} // namespace speaker
