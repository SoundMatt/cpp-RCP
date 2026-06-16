// fusa:req REQ-UDS-001
// fusa:req REQ-UDS-002
// fusa:req REQ-UDS-003
// fusa:req REQ-UDS-004

// UDS (Unified Diagnostic Services / ISO 14229) bridge interface stub (v0.38.0).
//
// Wraps RCP commands as UDS service requests (SID 0x31 RoutineControl).
// Requires a UDS stack integration.  All methods return errc::function_not_supported.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>

namespace rcp {
namespace udsbr {

struct Config {
    uint16_t routine_id{0x0100};
    std::chrono::milliseconds p2_timeout{50};   // default P2 server timeout
    std::chrono::milliseconds p2ext_timeout{5000}; // extended P2* timeout
};

class UdsController final : public rcp::Controller {
public:
    UdsController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<UdsController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<UdsController>(zone, std::move(cfg));
}

} // namespace udsbr
} // namespace rcp
