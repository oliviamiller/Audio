#include "portaudio.hpp"


void setupStreamFromConfig(const ConfigParams& params) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    // Determine device and get device info
    std::string new_device_name = params.device_name;
    PaDeviceIndex device_index = paNoDevice;
    const PaDeviceInfo* deviceInfo = nullptr;

    if (new_device_name.empty()) {
        device_index = audio_interface.getDefaultInputDevice();
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] No default input device found";
            throw std::runtime_error("no default input device found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (!deviceInfo) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get device info for default device";
            throw std::runtime_error("failed to get device info for default device");
        }
        if (!deviceInfo->name) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get the name of the default device";
            throw std::runtime_error("failed to get the name of the default device");
        }
        new_device_name = deviceInfo->name;

    } else {
        device_index = findDeviceByName(new_device_name, audio_interface);
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Audio input device with name '"
                               << new_device_name << "' not found";
            throw std::runtime_error("audio input device with name " + new_device_name + " not found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (!deviceInfo) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get device info for device: "
                               << new_device_name;
            throw std::runtime_error("failed to get device info for device: " + new_device_name);
        }
    }


    // Resolve final values (use params if specified, otherwise device defaults)
    int new_sample_rate = params.sample_rate.value_or(static_cast<int>(deviceInfo->defaultSampleRate));
    int new_num_channels = params.num_channels.value_or(1);
    double new_latency = params.latency_ms.has_value()
        ? params.latency_ms.value() / 1000.0  // Convert ms to seconds
        : deviceInfo->defaultLowInputLatency;

    // Validate num_channels against device's max input channels
    if (new_num_channels > deviceInfo->maxInputChannels) {
        VIAM_SDK_LOG(error) << "Requested " << new_num_channels << " channels but device '"
                            << deviceInfo->name << "' only supports " << deviceInfo->maxInputChannels
                            << " input channels";
        throw std::invalid_argument("num_channels exceeds device's maximum input channels");
    }

    // Check if config unchanged (only for reconfigure, not initial setup)
    if (stream_) {
        ActiveStreamConfig current_config{device_name_, sample_rate_, num_channels_, latency_};
        ActiveStreamConfig new_config{new_device_name, new_sample_rate, new_num_channels, new_latency};

        if (current_config == new_config) {
            VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Config unchanged, skipping stream restart";
            return;
        }
    }

    // This is initial setup, not reconfigure, start stream
    if (!stream_) {
        // Create audio context for initial setup
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
        int samples_per_chunk = new_sample_rate * CHUNK_DURATION_SECONDS;  // 100ms chunks
        auto new_audio_context = std::make_shared<AudioStreamContext>(info, samples_per_chunk);

        // Set configuration under lock before opening stream
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            device_name_ = new_device_name;
            device_index_ = device_index;
            sample_rate_ = new_sample_rate;
            num_channels_ = new_num_channels;
            latency_ = new_latency;
            audio_context_ = new_audio_context;
        }

        PaStream* new_stream = nullptr;
        // These will throw in case of error
        openStream(&new_stream);
        startStream(new_stream);

        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            stream_ = new_stream;
        }

        return;
    }

    // Config has changed, restart stream
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
    int samples_per_chunk = new_sample_rate * CHUNK_DURATION_SECONDS ;  // 100ms chunks
    auto new_audio_context = std::make_shared<AudioStreamContext>(info, samples_per_chunk);

    PaStream* old_stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        old_stream = stream_;
    }
    if (old_stream) shutdownStream(old_stream);

    // Set new configuration under lock (needed before openStream since it uses these)
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        device_name_ = new_device_name;
        device_index_ = device_index;
        sample_rate_ = new_sample_rate;
        num_channels_ = new_num_channels;
        latency_ = new_latency;
        audio_context_ = new_audio_context;
    }

    // Open and start new stream
    PaStream* new_stream = nullptr;
    openStream(&new_stream);
    startStream(new_stream);

    // Swap in new stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_ = new_stream;
    }

    VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Stream configured successfully";

}
