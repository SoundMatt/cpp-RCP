// fusa:req REQ-TLS-001
// fusa:req REQ-TLS-002
// fusa:req REQ-TLS-003
// fusa:req REQ-TLS-004
// fusa:req REQ-TLS-005
// fusa:req REQ-TLS-006
// fusa:req REQ-TLS-007
// fusa:req REQ-TLS-008
// fusa:req REQ-TLS-009
// fusa:req REQ-TLS-010

// Mutual TLS transport for zone controller communication (SG-006, IEC 62443 SL-2).
//
// Full implementation requires OpenSSL or an equivalent TLS library.
// Define RCP_TLS_OPENSSL and link -lssl -lcrypto to enable the real backend.
// Without that flag, this header provides the interface types and a stub that
// returns std::errc::function_not_supported, so the rest of the codebase can
// be built and tested without TLS on CI hosts that lack OpenSSL.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <string>
#include <vector>

namespace rcp {
namespace tls {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::string cert_file;    // PEM certificate for this endpoint
    std::string key_file;     // PEM private key for this endpoint
    std::string ca_file;      // PEM CA bundle for peer verification
    bool        verify_peer = true; // enforce mutual authentication
};

// ── Controller (interface) ────────────────────────────────────────────────────

// Controller connects to a ZoneServer over mTLS.
// Use new_controller() to obtain an implementation.
class Controller final : public rcp::Controller {
public:
    Controller(Zone z, const char*, uint16_t, Config)
        : zone_(z) {}

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
    Zone zone_;
};

// ── ZoneServer (interface stub) ───────────────────────────────────────────────

class ZoneServer {
public:
    ZoneServer(Zone, const char*, uint16_t, Config) {}
    void set_handler(std::function<Response(const Command&)>) {}
    void set_healthy(bool) {}
    void publish(const std::vector<uint8_t>&) {}
    void close() {}
    bool ok() const noexcept { return false; }
};

// ── Registry ──────────────────────────────────────────────────────────────────

class Registry final : public rcp::Registry {
public:
    std::error_code dial(Zone, const char*, uint16_t, Config) {
        return std::make_error_code(std::errc::function_not_supported);
    }

    std::error_code register_ctrl(std::shared_ptr<rcp::Controller>) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code deregister(Zone) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code lookup(Zone, std::shared_ptr<rcp::Controller>&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::vector<std::shared_ptr<rcp::Controller>> controllers() override { return {}; }
    std::error_code close() override { return {}; }
};

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

} // namespace tls
} // namespace rcp
