// fusa:req REQ-MQTT-001
// fusa:req REQ-MQTT-002
// fusa:req REQ-MQTT-003
// fusa:req REQ-MQTT-004

// MQTT protocol bridge interface stub (v0.36.0).
//
// Publishes commands to MQTT topics and receives responses from a
// correlation-ID-based reply topic.  Requires an MQTT client library
// (e.g. Eclipse Paho).  All methods return errc::function_not_supported.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <string>

namespace rcp {
namespace mqttbr {

struct Config {
    std::string broker_url{"tcp://localhost:1883"};
    std::string topic_prefix{"rcp"};
    int         qos{1};
    std::chrono::milliseconds timeout{1000};
};

class MqttController final : public rcp::Controller {
public:
    MqttController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

    Zone zone() const noexcept override { return zone_; }

    std::error_code send(const rcp::Context&, const Command&, Response&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }

    std::error_code subscribe(const rcp::Context&,
                               std::shared_ptr<StatusChannel>&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }

    std::error_code close() override { return {}; }

private:
    Zone   zone_;
    Config cfg_;
};

inline std::shared_ptr<MqttController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<MqttController>(zone, std::move(cfg));
}

} // namespace mqttbr
} // namespace rcp
