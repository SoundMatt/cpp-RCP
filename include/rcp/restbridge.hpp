// fusa:req REQ-REST-001
// fusa:req REQ-REST-002
// fusa:req REQ-REST-003
// fusa:req REQ-REST-004

// REST/HTTP protocol bridge interface stub.
//
// RestBridge maps RC-Client-level requests to HTTP POST
// /streams/{stream_key}/endpoints/{endpoint}. Requires an HTTP client
// backend (e.g. libcurl) to be linked separately. All methods return
// errc::function_not_supported until the adapter is linked.
//
// ROADMAP.md milestone 59, "Application-Layer Protocol Bridge Rebind
// (v2.15.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `restbridge.hpp` — translating between an
// RC Client's application-level view and a REST/HTTP endpoint is
// conceptually unaffected by which wire protocol RCP itself now speaks, so
// this is a mechanical interface update, not a redesign; the stub had no
// working behavior to preserve in the first place. The pre-replacement
// `RestController : public rcp::Controller` wrapper (keyed by the removed
// `Zone` type, and the old `/zones/{zone}/command` URL shape it implied)
// is dropped for a standalone `RestBridge`, the same "primitives, not a
// wrapped chokepoint" choice `rcp/authz.hpp` (v2.11.0) and
// `rcp/tsn.hpp`/`rcp/record.hpp`/`rcp/observe.hpp` (v2.14.0) already made —
// there is no unified client-side `send()` chokepoint left to wrap (that
// unification, if any, does not land until the CLI/capi/adapt rebuilds at
// v2.16.0, per those files' own notes). Addressing moves from `Zone` to
// the opaque per-connection `stream_key` (typically
// `avtp::StreamId::to_u64()`) plus `avtp::ByteBusId` pair every other
// Phase 14/15 header keys on since v2.10.0. `RestBridge::request` takes on
// the pre-replacement wrapper's `send()` role, shaped to match
// `rcp/record.hpp`'s/`rcp/observe.hpp`'s `RequestFn` — the same "new
// client-side send-equivalent call" shape as `rcp/udp.hpp`'s
// `Client::request` core signature — so a caller can address this bridge
// exactly where it would otherwise address a transport `Client`. The old
// `subscribe()`/`StatusChannel` method has no analog here: it belonged to
// `rcp::Controller`'s status-telemetry push model, which is not part of
// the target specification's request/response shape.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context only — see this header's own scope note above

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace restbridge {

struct Config {
    std::string base_url; // e.g. "http://localhost:8080"
    int         max_retries{3};
    std::chrono::milliseconds request_timeout{1000};
};

// RestBridge stubs the request/response side of a bridge between one RCP
// endpoint (`stream_key` + `endpoint`) and an HTTP POST call under
// `cfg.base_url`. No concrete HTTP client backend is linked here; every
// call reports errc::function_not_supported.
class RestBridge {
public:
    RestBridge(uint64_t stream_key, avtp::ByteBusId endpoint, Config cfg)
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

inline std::shared_ptr<RestBridge> new_bridge(uint64_t stream_key, avtp::ByteBusId endpoint, Config cfg) {
    return std::make_shared<RestBridge>(stream_key, endpoint, std::move(cfg));
}

} // namespace restbridge
} // namespace rcp
