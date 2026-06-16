// fusa:req REQ-REST-001
// fusa:req REQ-REST-002
// fusa:req REQ-REST-003
// fusa:req REQ-REST-004

// REST/HTTP protocol bridge interface stub (v0.32.0).
//
// RestController maps RCP commands to HTTP POST /zones/{zone}/command.
// Requires an HTTP client backend (e.g. libcurl) to be linked separately.
// All methods return errc::function_not_supported until the adapter is linked.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <string>

namespace rcp {
namespace restbridge {

struct Config {
    std::string base_url; // e.g. "http://localhost:8080"
    int         max_retries{3};
    std::chrono::milliseconds request_timeout{1000};
};

class RestController final : public rcp::Controller {
public:
    RestController(Zone zone, Config cfg)
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

inline std::shared_ptr<RestController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<RestController>(zone, std::move(cfg));
}

} // namespace restbridge
} // namespace rcp
