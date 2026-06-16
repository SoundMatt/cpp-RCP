// fusa:req REQ-DDS-001
// fusa:req REQ-DDS-002
// fusa:req REQ-DDS-003
// fusa:req REQ-DDS-004

// DDS (Data Distribution Service) protocol bridge interface stub (v0.35.0).
//
// Publishes RCP commands as DDS typed topics and collects replies from a
// dedicated reply topic.  Requires an OMG DDS implementation (e.g. FastDDS,
// Cyclone DDS).  All methods return errc::function_not_supported.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <string>

namespace rcp {
namespace ddsbr {

struct Config {
    std::string topic_prefix{"rcp"}; // DDS topic names: {prefix}/command, {prefix}/response
    int         domain_id{0};
    std::chrono::milliseconds timeout{500};
};

class DdsController final : public rcp::Controller {
public:
    DdsController(Zone zone, Config cfg) : zone_(zone), cfg_(std::move(cfg)) {}

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

inline std::shared_ptr<DdsController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<DdsController>(zone, std::move(cfg));
}

} // namespace ddsbr
} // namespace rcp
