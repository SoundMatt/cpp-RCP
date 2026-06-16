// fusa:req REQ-SOMEIP-001
// fusa:req REQ-SOMEIP-002
// fusa:req REQ-SOMEIP-003
// fusa:req REQ-SOMEIP-004

// SOME/IP protocol bridge interface stub (v0.33.0).
//
// Routes RCP commands over SOME/IP service discovery and method calls.
// All methods return errc::function_not_supported until the vsomeip adapter
// is linked (separate optional module).
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>

namespace rcp {
namespace someipbr {

struct Config {
    uint16_t service_id{0x0100};
    uint16_t instance_id{0x0001};
    uint16_t method_id{0x0001};
    std::chrono::milliseconds timeout{500};
};

class SomeIpController final : public rcp::Controller {
public:
    SomeIpController(Zone zone, Config cfg)
        : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<SomeIpController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<SomeIpController>(zone, std::move(cfg));
}

} // namespace someipbr
} // namespace rcp
