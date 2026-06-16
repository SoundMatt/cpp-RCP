// fusa:req REQ-GRPC-001
// fusa:req REQ-GRPC-002
// fusa:req REQ-GRPC-003
// fusa:req REQ-GRPC-004

// gRPC protocol bridge interface stub (v0.31.0).
//
// GrpcBridge translates between RCP wire frames and gRPC unary/streaming RPCs.
// Full implementation requires a generated gRPC stub from rcp.proto (not
// included here); this header defines the integration surface.
//
// Note: This is a compile-time interface stub.  All methods return
// errc::function_not_supported until a concrete gRPC adapter is linked.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <string>

namespace rcp {
namespace grpcbridge {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::string server_address; // e.g. "localhost:50051"
    int         max_retries{3};
    std::chrono::milliseconds rpc_timeout{1000};
};

// ── GrpcController ────────────────────────────────────────────────────────────

// GrpcController bridges RCP commands to a gRPC remote endpoint.
class GrpcController final : public rcp::Controller {
public:
    GrpcController(Zone zone, Config cfg)
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

inline std::shared_ptr<GrpcController> new_controller(Zone zone, Config cfg) {
    return std::make_shared<GrpcController>(zone, std::move(cfg));
}

} // namespace grpcbridge
} // namespace rcp
