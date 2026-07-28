// fusa:req REQ-REGMAP-001
// fusa:req REQ-REGMAP-002
// fusa:req REQ-REGMAP-003
// fusa:req REQ-REGMAP-004
// fusa:req REQ-REGMAP-005
// fusa:req REQ-REGMAP-006
// fusa:req REQ-REGMAP-007
// fusa:req REQ-REGMAP-008
// fusa:req REQ-REGMAP-009
// fusa:req REQ-REGMAP-010
// fusa:req REQ-REGMAP-011
// fusa:req REQ-REGMAP-012
// fusa:req REQ-REGMAP-013
// fusa:req REQ-REGMAP-014
// fusa:req REQ-REGMAP-015

// RC Server register-map data model and EP0 pseudo-endpoint — the
// whole-device configuration surface an OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC server exposes once it exists, addressed
// the same way any other endpoint's config is (extraction §3.6-§3.11,
// §4.1-§4.2, §5.1).
//
// ROADMAP.md milestone 45, "RC Server Lifecycle & Register-Map Model
// (v2.1.0)": this header defines the register-map fields and tables needed
// to bootstrap discovery (v2.2.0) and every endpoint milestone from v2.3.0
// onward, plus EP0 itself — the RC Server acting as a pseudo-endpoint with
// whole-map read/write and root-client arbitration. It rides on top of
// rcp/wire.hpp's StreamId/ByteBusId addressing (v2.0.0) and rcp/lifecycle.hpp's
// state machine (also v2.1.0); it does not depend on rcp/rcp.hpp's
// pre-replacement Zone/Command/Controller/Registry model at all.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above; no
// text from that document is reproduced here. Concrete field widths, the
// magic-number value, and table layout are this implementation's own
// encoding of that behavior for milestone 45 — full bit-for-bit register-map
// conformance against other TC18 implementations is not claimed, same as
// rcp/wire.hpp's equivalent disclaimer for the wire codec. Endpoint *types*
// (GPIO, SPI, I2C, ...) and their functional config contents are out of
// scope here — this milestone only defines the generic/functional config
// split and an opaque byte-blob functional config slot; interpreting that
// slot's contents per endpoint type is v2.3.0 onward.
//
// ROADMAP.md milestone 50, "E2E CRC Safe Points & Safety-Request Variants
// (v2.6.0)": RequestStreamConfig's per-stream watchdog/safe-state fields
// and EndpointGenericConfig's per-endpoint CRC-enable toggles are expanded
// to their full field set at this milestone, superseding the three
// placeholder fields v2.1.0 reserved layout for. The behavior that reads
// and acts on these fields lives in rcp/e2e.hpp and rcp/sequencer.hpp, not
// here — same config-vs-behavior split as everything else in this header.
#pragma once

#include <rcp/lifecycle.hpp>
#include <rcp/wire.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace regmap {

// ── EndpointId ────────────────────────────────────────────────────────────────
// Identifies one configured endpoint slot in the register map. EP0 (below)
// is the reserved id for the RC Server's own pseudo-endpoint; real endpoints
// (GPIO, SPI, ...) are assigned ids starting at 1 by later milestones.

using EndpointId = uint16_t;
constexpr EndpointId kEp0 = 0;

// ── Errors ────────────────────────────────────────────────────────────────────
// The four mandatory error codes needed once register access exists
// (extraction §3.15).

enum class RegMapErrc : int {
    unauthorized_access = 1, // client is neither the root client nor this endpoint's owner
    locked_mem_access    = 2, // target config block is locked by the current lifecycle state
    request_rejected     = 3, // request is well-formed but the server declines it (e.g. root already claimed)
    invalid_parameter     = 4, // request references an out-of-range endpoint/table entry or bad value
};

inline const std::error_category& regmap_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap"; }
        std::string message(int ev) const override {
            switch (static_cast<RegMapErrc>(ev)) {
            case RegMapErrc::unauthorized_access: return "rcp/regmap: UNAUTHORIZED_ACCESS";
            case RegMapErrc::locked_mem_access:    return "rcp/regmap: LOCKED_MEM_ACCESS";
            case RegMapErrc::request_rejected:     return "rcp/regmap: REQUEST_REJECTED";
            case RegMapErrc::invalid_parameter:     return "rcp/regmap: INVALID_PARAMETER";
            default:                                return "rcp/regmap: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(RegMapErrc e) noexcept {
    return {static_cast<int>(e), regmap_category()};
}

// ── Generic vs. functional endpoint config split ──────────────────────────────
// New relative to the pre-replacement design (extraction §4.2 vs. §4.4/§5.x):
// every configured endpoint has two independent config blocks. The generic
// block is server-owned and endpoint-type-agnostic (which HW pins it claims,
// how deep its request/response queues are); the functional block is
// endpoint-type-specific and is left as an opaque byte blob here since no
// endpoint type is implemented until v2.3.0 onward. Every later endpoint
// milestone depends on this split already existing (extraction §6 item 5).

struct EndpointGenericConfig {
    std::vector<uint16_t> hw_pin_indices; // indices into RegisterMap::hw_pin_map this endpoint claims
    uint16_t              request_queue_size  = 0;
    uint16_t              response_queue_size = 0;

    // Per-endpoint opt-in E2E CRC "safe mode" (extraction §4.4, §4.7),
    // ROADMAP.md milestone 50 (v2.6.0) — independently toggled per message
    // role, since a given endpoint's request/ack/response traffic can have
    // different integrity needs. Behavior lives in rcp/e2e.hpp; this struct
    // only owns the durable configuration bits, same split as every other
    // config-vs-behavior boundary in this codebase.
    bool ep_req_crc_enable      = false;
    bool ep_ack_crc_enable      = false;
    bool ep_response_crc_enable = false;
};

struct EndpointFunctionalConfig {
    std::vector<uint8_t> data; // endpoint-type-specific; interpreted starting at v2.3.0
};

// ── HW pin-mapping config (extraction §3.7) ──────────────────────────────────

struct HwPinMapEntry {
    uint16_t pin_id   = 0; // physical pin identifier; numbering is implementation-defined
    uint8_t  function = 0; // pin function/mode selector; meaning is endpoint-type-defined
};

// ── Sequencer-state registers (extraction §3.11, §3.16) ──────────────────────
// Persistent 8-bit values; behavior lives in rcp/sequencer.hpp (v2.5.0).
// Declared here, ahead of RequestStreamConfig below, since that struct's
// rx_safe_sequencer_state field (v2.6.0) needs the type name already
// in scope.

using SequencerState = uint8_t;

// ── rx_safety_measure selector (extraction §3.8) ──────────────────────────────
// Which mechanism a safety-tagged (0x8x) request drives the endpoint
// through once it is in safe state — ROADMAP.md milestone 50 (v2.6.0).
// Behavior lives in rcp/e2e.hpp; this enum is the durable register value
// selecting between the two.

enum class RxSafetyMeasure : uint8_t {
    ForceHighImpedance = 0, // hold outputs high-impedance; no sequencer consulted
    RunSafeSequencer   = 1, // "safe" is rx_safestate_sequencer reading rx_safe_sequencer_state
};

// ── Request-stream config (extraction §3.8) ──────────────────────────────────
// The full per-request-stream watchdog and safe-state register set
// ROADMAP.md milestone 50 (v2.6.0) calls for. Behavior that reads and acts
// on these fields lives in rcp/e2e.hpp (watchdog overflow, CRC
// enforcement, sequence checking, safe-state gating) and
// rcp/sequencer.hpp (the 0x8x safety-tagged request variants these fields
// exist to support) — this struct is durable storage only, same
// config-vs-behavior split used throughout this header.
//
// This supersedes the three placeholder fields (rx_wd_timeout_s,
// rx_wd_action, rx_safety_measure as a bare uint8_t) v2.1.0 added purely to
// reserve register-map layout ahead of this milestone's real field list.

struct RequestStreamConfig {
    wire::StreamId stream_id{};
    uint16_t       queue_size = 0;

    // Watchdog (extraction §3.8). rx_wd_timeout_interval's unit
    // (milliseconds) is this implementation's own choice, same as every
    // other concrete-width decision elsewhere in this header.
    uint32_t rx_wd_timeout_interval = 0;
    bool     rx_wd_enable           = false;
    bool     rx_wd_safestate_enable = false; // watchdog overflow drives the endpoint into safe state
    bool     rx_wd_info_enable      = false; // repeating notification while latched in safe state

    // E2E CRC enforcement (extraction §3.8, §4.7): per-request drop vs.
    // whole-stream latch on a CRC_ERROR outcome.
    bool rx_enforce_e2e = false;

    // Monotonic sequence-number check — orthogonal to the watchdog above; a
    // stream can enforce either, both, or neither independently.
    bool rx_enforce_seq          = false;
    bool rx_seq_safestate_enable = false;

    // Request-queue overrun is a distinct trigger from watchdog expiry that
    // can also be configured to drive the endpoint into safe state.
    bool rx_ovrflw_safestate_enable = false;

    // Which mechanism a safety-tagged request drives the endpoint through
    // once in safe state, and (for RunSafeSequencer only) which sequencer
    // and target state together define "safe".
    RxSafetyMeasure rx_safety_measure      = RxSafetyMeasure::ForceHighImpedance;
    uint16_t        rx_safestate_sequencer = 0;
    SequencerState  rx_safe_sequencer_state = 0;
};

// ── EP-ID / byte_bus_id mapping table (extraction §3.9) ──────────────────────
// Risk, flagged explicitly per the roadmap: the *order* in which a client
// populates this table is client-guaranteed, not something this server
// implementation re-derives or verifies on its own — a client that writes
// entries out of the order it intends them to be interpreted in will not be
// corrected here. Callers that need order-independence must encode an
// explicit ordering key inside EpIdMappingEntry themselves; this milestone
// does not add one, since no endpoint type exists yet to define what
// "correct order" would even mean.

struct EpIdMappingEntry {
    EndpointId      ep_id       = 0;
    wire::ByteBusId byte_bus_id = 0;
};

// ── Response / ack queue config (extraction §3.10) ───────────────────────────

struct ResponseQueueConfig {
    uint16_t response_queue_size = 0;
    uint16_t ack_queue_size      = 0;
};

// ── svr_implemented_options bitmask ──────────────────────────────────────────
// Advertises which optional protocol features this server implements. Bits
// are reserved here for features whose actual implementation lands in later
// milestones; a server MUST NOT set a bit for a feature it does not yet
// implement.

constexpr uint32_t kOptConditionalRequests = 0x0000'0001; // compound/triggered/timed/chained requests (v2.5.0)
constexpr uint32_t kOptSafetyRequests      = 0x0000'0002; // E2E CRC safe points + safety-request variants (v2.6.0)
constexpr uint32_t kOptFragmentation       = 0x0000'0004; // multi-segment requests — bit reserved, never set: ROADMAP.md milestone 52's already-decided fragmentation no-go (v2.8.0)

// ── TablePointer ──────────────────────────────────────────────────────────────
// The pointer/capacity pair pattern used for each of the five bootstrap
// tables below (extraction §3.6). `offset` is this implementation's own
// choice of addressing unit (byte offset into the register map); `capacity`
// is the number of entries the table can hold, not necessarily how many are
// populated.

struct TablePointer {
    uint32_t offset   = 0;
    uint16_t capacity = 0;
};

// ── RegisterMap ───────────────────────────────────────────────────────────────
// The general register-map fields needed to bootstrap everything else
// (extraction §3.6), plus the in-memory contents of the tables those
// pointer/capacity pairs describe. A real server would serialize this to
// and from the wire on demand; this milestone models the data, not the wire
// encoding of the whole map (individual field reads/writes ride on the
// wire::ByteBusId-addressed standard request already implemented in
// rcp/wire.hpp).

constexpr uint32_t kRegisterMapMagic = 0x52435030; // "RCP0" — this implementation's own placeholder value

struct RegisterMap {
    // General / bootstrap fields.
    uint32_t magic                 = kRegisterMapMagic;
    uint8_t  protocol_version_major = 0;
    uint8_t  protocol_version_minor = 0;
    uint16_t vendor_id             = 0;
    uint16_t device_id             = 0;
    uint16_t endpoint_count        = 0;
    uint16_t max_streams           = 0;
    uint16_t max_queue_depth       = 0;
    uint32_t svr_implemented_options = 0;

    // Pointer/capacity fields for the five bootstrap tables.
    TablePointer hw_pin_map_table{};
    TablePointer request_stream_table{};
    TablePointer response_stream_table{};
    TablePointer ep_id_mapping_table{};
    TablePointer functional_config_table{};

    // Table contents.
    std::vector<HwPinMapEntry>          hw_pin_map;
    std::vector<RequestStreamConfig>    request_streams;
    std::vector<ResponseQueueConfig>    response_streams;
    std::vector<EpIdMappingEntry>       ep_id_mapping;
    std::vector<EndpointFunctionalConfig> functional_configs;

    // Per-endpoint generic config, indexed in parallel with functional_configs
    // (index i is EndpointId i+1 — EP0 is not stored here, see Ep0 below).
    std::vector<EndpointGenericConfig> generic_configs;

    // Persistent sequencer-state storage (extraction §3.11, §3.16).
    std::vector<SequencerState> sequencer_states;
};

// ── EP0 — RC Server as a pseudo-endpoint ─────────────────────────────────────
// extraction §5.1, §4.1: the RC Server itself is addressable like any other
// endpoint (as EP0) for whole-register-map reads and writes. Every client may
// read the whole map. Only one client — the root client, identified by
// svr_root_client_index — may write the whole map through EP0; every other
// client is restricted to writing the generic/functional config of the
// endpoint(s) it owns.
//
// "Client" here is identified by an opaque index the embedding transport
// assigns per connected stream; this header does not know or care how that
// index maps to a wire::StreamId, only that it is stable for the lifetime of
// the connection.
class Ep0 {
public:
    // ConfigBlock distinguishes which of an endpoint's two config blocks a
    // write targets, since the two lock at different lifecycle states (see
    // ServerLifecycle::generic_config_locked/functional_config_locked).
    enum class ConfigBlock { Generic, Functional };

    Ep0(RegisterMap& regs, lifecycle::ServerLifecycle& lc) noexcept
        : regs_(regs), lifecycle_(lc), endpoint_owner_(regs.endpoint_count) {}

    // claim_root_client implements the exclusive root-client rule: once a
    // stream holds the root-client slot, a different stream's claim is
    // rejected until release_root_client() is called (e.g. on disconnect).
    // The same stream re-claiming its own slot is a harmless no-op.
    std::error_code claim_root_client(size_t client) noexcept {
        if (root_client_.has_value() && *root_client_ != client)
            return make_error_code(RegMapErrc::request_rejected);
        root_client_ = client;
        return {};
    }

    void release_root_client() noexcept { root_client_.reset(); }

    bool is_root_client(size_t client) const noexcept {
        return root_client_.has_value() && *root_client_ == client;
    }

    std::optional<size_t> root_client() const noexcept { return root_client_; }

    // set_endpoint_owner assigns which client's writes are accepted for a
    // given non-EP0 endpoint's config (assigned during discovery/config in
    // earlier real deployments; exposed directly here since discovery itself
    // is a later milestone, v2.2.0).
    std::error_code set_endpoint_owner(EndpointId ep, size_t client) noexcept {
        if (ep == kEp0 || ep > endpoint_owner_.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        endpoint_owner_[static_cast<size_t>(ep - 1)] = client;
        return {};
    }

    // check_read_access: reads of the whole map (EP0) or of any single
    // endpoint's config are unrestricted — every connected client may read
    // (extraction §5.1's "whole-register-map read" is granted to any
    // client, not just root).
    std::error_code check_read_access(EndpointId target) const noexcept {
        if (target != kEp0 && target > endpoint_owner_.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        return {};
    }

    // check_write_access enforces: EP0 (whole-map) writes require the root
    // client; a non-EP0 endpoint's write requires either the root client or
    // that endpoint's assigned owner; and a write to whichever config block
    // is targeted is rejected while the current lifecycle state has that
    // specific block locked, regardless of who is asking.
    std::error_code check_write_access(size_t client, EndpointId target,
                                        ConfigBlock block = ConfigBlock::Generic) const noexcept {
        if (target == kEp0) {
            return is_root_client(client) ? std::error_code{}
                                           : make_error_code(RegMapErrc::unauthorized_access);
        }
        if (target > endpoint_owner_.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        const auto& owner = endpoint_owner_[static_cast<size_t>(target - 1)];
        if (!is_root_client(client) && (!owner.has_value() || *owner != client))
            return make_error_code(RegMapErrc::unauthorized_access);
        const bool locked = (block == ConfigBlock::Generic)
                                 ? lifecycle_.generic_config_locked()
                                 : lifecycle_.functional_config_locked();
        if (locked) {
            return make_error_code(RegMapErrc::locked_mem_access);
        }
        return {};
    }

    // read_whole_map is the unrestricted-read half of EP0's whole-register-
    // map read/write behavior (extraction §5.1) — any connected client may
    // call this.
    const RegisterMap& read_whole_map() const noexcept { return regs_; }

    // write_whole_map is the root-client-only half of the same rule.
    std::error_code write_whole_map(size_t client, RegisterMap new_map) noexcept {
        if (!is_root_client(client))
            return make_error_code(RegMapErrc::unauthorized_access);
        regs_ = std::move(new_map);
        return {};
    }

    // write_generic_config / write_functional_config apply the per-endpoint
    // write-access and per-block lock checks above before mutating the
    // targeted endpoint's config block in the underlying RegisterMap.
    std::error_code write_generic_config(size_t client, EndpointId target,
                                          EndpointGenericConfig cfg) noexcept {
        if (target == kEp0) return make_error_code(RegMapErrc::invalid_parameter);
        auto ec = check_write_access(client, target, ConfigBlock::Generic);
        if (ec) return ec;
        regs_.generic_configs[static_cast<size_t>(target - 1)] = std::move(cfg);
        return {};
    }

    std::error_code write_functional_config(size_t client, EndpointId target,
                                             EndpointFunctionalConfig cfg) noexcept {
        if (target == kEp0) return make_error_code(RegMapErrc::invalid_parameter);
        auto ec = check_write_access(client, target, ConfigBlock::Functional);
        if (ec) return ec;
        regs_.functional_configs[static_cast<size_t>(target - 1)] = std::move(cfg);
        return {};
    }

private:
    RegisterMap&                       regs_;
    lifecycle::ServerLifecycle&        lifecycle_;
    std::optional<size_t>              root_client_;
    std::vector<std::optional<size_t>> endpoint_owner_;
};

} // namespace regmap
} // namespace rcp

// Enable std::error_code construction from rcp::regmap::RegMapErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::regmap::RegMapErrc> : true_type {};
} // namespace std
