// fusa:req REQ-TSN-001
// fusa:req REQ-TSN-002
// fusa:req REQ-TSN-003
// fusa:req REQ-TSN-004
// fusa:req REQ-TSN-005
// fusa:req REQ-TSN-006

// IEEE 802.1Qbv-aware UDP transport adapter for hard real-time Ethernet.
//
// Maps rcp::Priority values to IEEE 802.1p PCP (Priority Code Point) classes
// and applies SO_PRIORITY on the UDP socket so the Linux traffic shaper routes
// frames into the correct egress queue.
//
// Full 802.1Qbv gate scheduling requires a TSN-capable NIC and kernel ≥ 4.15.
// On standard hardware this provides best-effort priority mapping only.
//
// Wraps udp::Controller; add #include <rcp/udp.hpp> before this header.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <memory>

#if defined(__linux__)
#  include <sys/socket.h>
#  define RCP_TSN_SO_PRIORITY 1
#endif

namespace rcp {
namespace tsn {

// ── PCPMap ────────────────────────────────────────────────────────────────────

// PCPMap maps each rcp::Priority to an IEEE 802.1p PCP value (0–7).
// PCP 7 is the highest priority (used for Priority::Critical).
struct PCPMap {
    uint8_t normal   = 2; // PCP for Priority::Normal
    uint8_t high     = 5; // PCP for Priority::High
    uint8_t critical = 7; // PCP for Priority::Critical
};

inline PCPMap default_pcp_map() { return PCPMap{}; }

inline uint8_t pcp_for(const PCPMap& m, Priority p) noexcept {
    switch (p) {
    case Priority::High:     return m.high;
    case Priority::Critical: return m.critical;
    default:                 return m.normal;
    }
}

// ── TSNConfig ─────────────────────────────────────────────────────────────────

struct TSNConfig {
    PCPMap  pcp_map;
    int     vlan_id    = 0;   // 0 = untagged
    int     cycle_ns   = 0;   // 802.1Qbv gate cycle in nanoseconds (0 = disabled)
};

inline TSNConfig default_tsn_config() { return TSNConfig{}; }

// ── Controller wrapper ────────────────────────────────────────────────────────

// Controller wraps any rcp::Controller and applies SO_PRIORITY to the kernel
// socket before each send() so the egress qdisc places the frame in the correct
// 802.1p traffic class.
class Controller final : public rcp::Controller {
public:
    Controller(std::shared_ptr<rcp::Controller> inner,
               int                              socket_fd,
               TSNConfig                        cfg = {})
        : inner_(std::move(inner)), fd_(socket_fd), cfg_(cfg) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
#if defined(RCP_TSN_SO_PRIORITY)
        int pcp = static_cast<int>(pcp_for(cfg_.pcp_map, cmd.priority));
        if (fd_ >= 0)
            ::setsockopt(fd_, SOL_SOCKET, SO_PRIORITY, &pcp, sizeof(pcp));
#endif
        return inner_->send(ctx, cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override { return inner_->close(); }

private:
    std::shared_ptr<rcp::Controller> inner_;
    [[maybe_unused]] int fd_;
    TSNConfig cfg_;
};

inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner,
                                                    int socket_fd,
                                                    TSNConfig cfg = {}) {
    return std::make_shared<Controller>(std::move(inner), socket_fd, cfg);
}

} // namespace tsn
} // namespace rcp
