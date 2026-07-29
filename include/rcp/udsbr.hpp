// fusa:req REQ-UDS-001
// fusa:req REQ-UDS-002
// fusa:req REQ-UDS-003
// fusa:req REQ-UDS-004

// UDS (Unified Diagnostic Services / ISO 14229) bridge interface stub.
//
// Wraps RC-Client-level requests as UDS service requests (SID 0x31
// RoutineControl). Requires a UDS stack integration. All methods return
// errc::function_not_supported until a concrete backend is linked.
//
// ROADMAP.md milestone 59, "Application-Layer Protocol Bridge Rebind
// (v2.15.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `udsbr.hpp` — translating between an RC
// Client's application-level view and a UDS service is conceptually
// unaffected by which wire protocol RCP itself now speaks, so this is a
// mechanical interface update, not a redesign; the stub had no working
// behavior to preserve in the first place. The pre-replacement
// `UdsController : public rcp::Controller` wrapper (keyed by the removed
// `Zone` type) is dropped for a standalone `UdsBridge`, the same
// "primitives, not a wrapped chokepoint" choice `rcp/authz.hpp` (v2.11.0)
// and `rcp/tsn.hpp`/`rcp/record.hpp`/`rcp/observe.hpp` (v2.14.0) already
// made — there is no unified client-side `send()` chokepoint left to wrap
// (that unification, if any, does not land until the CLI/capi/adapt
// rebuilds at v2.16.0, per those files' own notes). Addressing moves from
// `Zone` to the opaque per-connection `stream_key` (typically
// `avtp::StreamId::to_u64()`) plus `avtp::ByteBusId` pair every other
// Phase 14/15 header keys on since v2.10.0. `UdsBridge::request` takes on
// the pre-replacement wrapper's `send()` role, shaped to match
// `rcp/record.hpp`'s/`rcp/observe.hpp`'s `RequestFn` — the same "new
// client-side send-equivalent call" shape as `rcp/udp.hpp`'s
// `Client::request` core signature — so a caller can address this bridge
// exactly where it would otherwise address a transport `Client`. The old
// `subscribe()`/`StatusChannel` method has no analog here: it belonged to
// `rcp::Controller`'s status-telemetry push model, which is not part of
// the target specification's request/response shape.
//
// This header bridges into UDS, the diagnostic-services ecosystem; it is
// unrelated to `rcp/doipbr.hpp`'s DoIP transport encapsulation beyond the
// fact that DoIP commonly carries UDS requests over TCP/IP — each header
// stubs its own side of that pairing independently.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context only — see this header's own scope note above

#include <chrono>
#include <cstdint>
#include <memory>
#include <system_error>
#include <vector>

namespace rcp {
namespace udsbr {

struct Config {
    uint16_t routine_id{0x0100};
    std::chrono::milliseconds p2_timeout{50};   // default P2 server timeout
    std::chrono::milliseconds p2ext_timeout{5000}; // extended P2* timeout
};

// UdsBridge stubs the request/response side of a bridge between one RCP
// endpoint (`stream_key` + `endpoint`) and a UDS RoutineControl request
// against `cfg.routine_id`. No concrete UDS stack is linked here; every
// call reports errc::function_not_supported.
class UdsBridge {
public:
    UdsBridge(uint64_t stream_key, avtp::ByteBusId endpoint, Config cfg)
        : stream_key_(stream_key), endpoint_(endpoint), cfg_(std::move(cfg)) {}

    uint64_t stream_key() const noexcept { return stream_key_; }
    avtp::ByteBusId endpoint() const noexcept { return endpoint_; }

    // request is this bridge's client-side send-equivalent call — see this
    // file's header comment for why its shape matches rcp/record.hpp's and
    // rcp/observe.hpp's RequestFn.
    std::error_code request(const rcp::Context&, const acf::AcfMessageInfo&,
                             const std::vector<uint8_t>&,
                             acf::AcfMessageInfo&, std::vector<uint8_t>&) {
        return std::make_error_code(std::errc::function_not_supported);
    }

    std::error_code close() { return {}; }

private:
    uint64_t         stream_key_;
    avtp::ByteBusId  endpoint_;
    Config           cfg_;
};

inline std::shared_ptr<UdsBridge> new_bridge(uint64_t stream_key, avtp::ByteBusId endpoint, Config cfg) {
    return std::make_shared<UdsBridge>(stream_key, endpoint, std::move(cfg));
}

} // namespace udsbr
} // namespace rcp
