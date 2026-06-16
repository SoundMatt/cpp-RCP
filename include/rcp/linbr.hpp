// fusa:req REQ-LIN-001
// fusa:req REQ-LIN-002
// fusa:req REQ-LIN-003
// fusa:req REQ-LIN-004

// LIN (Local Interconnect Network) bus bridge interface stub (v0.37.0).
//
// Maps RCP commands to LIN master-frame requests via a SocketCAN LIN driver
// or dedicated LIN hardware API.  All methods return errc::function_not_supported.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>

namespace rcp {
namespace linbr {

struct Config {
    uint8_t frame_id{0x10};
    std::chrono::milliseconds timeout{50};
};

class LinController final : public rcp::Controller {
public:
    LinController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<LinController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<LinController>(zone, std::move(cfg));
}

} // namespace linbr
} // namespace rcp
