#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/config/resource.hpp>
#include "audio_utils.hpp"
#include "test_utils.hpp"

using namespace viam::sdk;
using namespace audio;

class AudioUtilsTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};

class AudioUtilsTest : public test_utils::AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();
    }
};

TEST_F(AudioUtilsTest, ParseConfigAttributesEmpty) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesDeviceName) {
    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("Test Device");

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "Test Device");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesSampleRate) {
    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = 48000.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    ASSERT_TRUE(params.sample_rate.has_value());
    EXPECT_EQ(params.sample_rate.value(), 48000);
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesNumChannels) {
    auto attributes = ProtoStruct{};
    attributes["num_channels"] = 2.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    ASSERT_TRUE(params.num_channels.has_value());
    EXPECT_EQ(params.num_channels.value(), 2);
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesLatency) {
    auto attributes = ProtoStruct{};
    attributes["latency"] = 100.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    ASSERT_TRUE(params.latency_ms.has_value());
    EXPECT_EQ(params.latency_ms.value(), 100.0);
}

TEST_F(AudioUtilsTest, ParseConfigAttributesAll) {
    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("My Device");
    attributes["sample_rate"] = 44100.0;
    attributes["num_channels"] = 1.0;
    attributes["latency"] = 50.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "My Device");
    ASSERT_TRUE(params.sample_rate.has_value());
    EXPECT_EQ(params.sample_rate.value(), 44100);
    ASSERT_TRUE(params.num_channels.has_value());
    EXPECT_EQ(params.num_channels.value(), 1);
    ASSERT_TRUE(params.latency_ms.has_value());
    EXPECT_EQ(params.latency_ms.value(), 50.0);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigUsesDefaults) {
    using ::testing::Return;

    audio::utils::ConfigParams params;
    params.device_name = "";  // Use default device

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.device_index, 0);
    EXPECT_EQ(stream_params.device_name, test_utils::AudioTestBase::testDeviceName);
    EXPECT_EQ(stream_params.sample_rate, 44100);
    EXPECT_EQ(stream_params.num_channels, 1); 
    EXPECT_TRUE(stream_params.is_input);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigUsesProvidedValues) {
    audio::utils::ConfigParams params;
    params.device_name = "";
    params.sample_rate = 48000;
    params.num_channels = 2;
    params.latency_ms = 100.0;

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.sample_rate, 48000);
    EXPECT_EQ(stream_params.num_channels, 2);
    EXPECT_DOUBLE_EQ(stream_params.latency_seconds, 0.1);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigOutputDirection) {
    using ::testing::Return;

    ON_CALL(*mock_pa_, getDefaultOutputDevice())
        .WillByDefault(Return(0));

    audio::utils::ConfigParams params;
    params.device_name = "";

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Output,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_FALSE(stream_params.is_input);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigThrowsOnExcessiveChannels) {
    audio::utils::ConfigParams params;
    params.device_name = "";
    params.num_channels = 10;  // More than mock_device_info_.maxInputChannels (2)

    EXPECT_THROW({
        audio::utils::setupStreamFromConfig(
            params,
            audio::utils::StreamDirection::Input,
            nullptr,
            mock_pa_.get()
        );
    }, std::invalid_argument);
}

TEST_F(AudioUtilsTest, FindDeviceByNameFindsDevice) {
    using ::testing::Return;

    PaDeviceInfo device1;
    device1.name = "Device 1";
    PaDeviceInfo device2;
    device2.name = "Device 2";

    ON_CALL(*mock_pa_, getDeviceCount())
        .WillByDefault(Return(2));

    EXPECT_CALL(*mock_pa_, getDeviceInfo(0))
        .WillRepeatedly(Return(&device1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1))
        .WillRepeatedly(Return(&device2));

    PaDeviceIndex idx = audio::utils::findDeviceByName("Device 2", *mock_pa_);
    EXPECT_EQ(idx, 1);
}

TEST_F(AudioUtilsTest, FindDeviceByNameReturnsNoDeviceWhenNotFound) {
    using ::testing::Return;

    PaDeviceInfo device1;
    device1.name = "Device 1";

    ON_CALL(*mock_pa_, getDeviceCount())
        .WillByDefault(Return(1));

    EXPECT_CALL(*mock_pa_, getDeviceInfo(0))
        .WillRepeatedly(Return(&device1));

    PaDeviceIndex idx = audio::utils::findDeviceByName("Nonexistent", *mock_pa_);
    EXPECT_EQ(idx, paNoDevice);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new AudioUtilsTestEnvironment);
  return RUN_ALL_TESTS();
}
