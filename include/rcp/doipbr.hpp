// fusa:req REQ-DOIP-001
// fusa:req REQ-DOIP-002
// fusa:req REQ-DOIP-003
// fusa:req REQ-DOIP-004

// DoIP (Diagnostics over IP / ISO 13400) bridge interface stub (v0.39.0).
//
// Encapsulates UDS requests inside DoIP diagnostic messages over TCP/IP.
// Requires a DoIP stack integration.  All methods return errc::function_not_supported.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rcp {
namespace doipbr {

struct Config {
    std::string server_ip;  // must be set by caller — no default IP
    uint16_t    server_port{13400};
    uint16_t    logical_addr{0x0001};
    std::chrono::milliseconds tcp_timeout{2000};
};

class DoIpController final : public rcp::Controller {
public:
    DoIpController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<DoIpController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<DoIpController>(zone, std::move(cfg));
}

} // namespace doipbr
} // namespace rcp
