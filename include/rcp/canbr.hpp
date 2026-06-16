// fusa:req REQ-CAN-001
// fusa:req REQ-CAN-002
// fusa:req REQ-CAN-003
// fusa:req REQ-CAN-004

// CAN / CAN-FD protocol bridge interface stub (v0.34.0).
//
// Maps RCP commands to CAN frames via SocketCAN (Linux) or a hardware CAN
// driver.  All methods return errc::function_not_supported until the adapter
// is linked.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>

namespace rcp {
namespace canbr {

struct Config {
    uint32_t can_id_base{0x100}; // base arbitration ID
    bool     fd_mode{false};     // CAN-FD frames
    std::chrono::milliseconds timeout{100};
};

class CanController final : public rcp::Controller {
public:
    CanController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<CanController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<CanController>(zone, std::move(cfg));
}

} // namespace canbr
} // namespace rcp
