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
//
// c-RCP-derived content ported in this batch (Phase 17 / cpp-RCP issue #129,
// "Phase 4 batch A" — see this file's own "Phase 4 batch A" banner below):
// fusa:req REQ-RMAP-001
// fusa:req REQ-RMAP-003
// fusa:req REQ-RMAP-009
// fusa:req REQ-RMAP-010
// fusa:req REQ-RMAP-011
// fusa:req REQ-RMAP-012
// fusa:req REQ-RMAP-016
// fusa:req REQ-RMAP-023
// fusa:req REQ-RMAP-024
// fusa:req REQ-RMAP-025
// fusa:req REQ-RMAP-026
// fusa:req REQ-RMAP-027
// fusa:req REQ-RMAP-028
// fusa:req REQ-RMAP-029
// fusa:req REQ-RMAP-030
// fusa:req REQ-RMAP-031
// fusa:req REQ-RMAP-032
// fusa:req REQ-RMAP-033
// fusa:req REQ-RMAP-034
// fusa:req REQ-RMAP-035
// fusa:req REQ-RMAP-036
// fusa:req REQ-RMAP-037
// fusa:req REQ-RMAP-038
// fusa:req REQ-RMAP-039
// fusa:req REQ-RMAP-066
// fusa:req REQ-RMAP-067
// fusa:req REQ-RMAP-070
// fusa:req REQ-RMAP-073
// fusa:req REQ-RMAP-074
// fusa:req REQ-RMAP-075
// fusa:req REQ-RMAP-076
// fusa:req REQ-RMAP-077
// fusa:req REQ-RMAP-078
// fusa:req REQ-RMAP-079
// fusa:req REQ-RMAP-081
// fusa:req REQ-RMAP-086
// fusa:req REQ-RMAP-087
//
// c-RCP-derived content ported in Phase 4 batch B (this same issue/roadmap
// entry — see this file's own "Phase 4 batch B" banner below):
// fusa:req REQ-RMAP-017
// fusa:req REQ-RMAP-040
// fusa:req REQ-RMAP-041
// fusa:req REQ-RMAP-042
// fusa:req REQ-RMAP-043
// fusa:req REQ-RMAP-044
// fusa:req REQ-RMAP-045
// fusa:req REQ-RMAP-047
// fusa:req REQ-RMAP-048
// fusa:req REQ-RMAP-049
// fusa:req REQ-RMAP-050
// fusa:req REQ-RMAP-051
// fusa:req REQ-RMAP-052
// fusa:req REQ-RMAP-053
// fusa:req REQ-RMAP-054
// fusa:req REQ-RMAP-056
// fusa:req REQ-RMAP-057
// fusa:req REQ-RMAP-058
// fusa:req REQ-RMAP-059
// fusa:req REQ-RMAP-060
// fusa:req REQ-RMAP-061
// fusa:req REQ-RMAP-063
// fusa:req REQ-RMAP-064
// fusa:req REQ-RMAP-065
// fusa:req REQ-RMAP-071
// fusa:req REQ-RMAP-083
// fusa:req REQ-RMAP-084
// fusa:req REQ-WAKEUP-020
// fusa:req REQ-E2E-029
// fusa:req REQ-E2E-030
// fusa:req REQ-E2E-045
// fusa:req REQ-E2E-046
// fusa:req REQ-LIFECYCLE-025
// fusa:req REQ-LIFECYCLE-031
// fusa:req REQ-SEQ-013

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
// rcp/avtp.hpp's StreamId/ByteBusId addressing (v2.0.0) and rcp/lifecycle.hpp's
// state machine (also v2.1.0); it does not depend on rcp/rcp.hpp's
// pre-replacement Zone/Command/Controller/Registry model at all.
//
// ── Phase 4 batch A rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17") ─────
// c-RCP (this project's RC5-conformant reference implementation) models
// roughly 7x more of TC18's actual register-map content than this header's
// pre-rewrite v2.x design did (ROADMAP.md's own Phase 17 rationale). This
// pass re-derives, from c-RCP's CURRENT src/regmap.c/include/rcp/regmap.h,
// exactly the subset ROADMAP.md Phase 4 batch A scopes: EP0/root-client
// concepts, the sub-table pointer/capacity pattern, svr_implemented_options
// (REQ-RMAP-030), the general register map (GeneralMap, c-RCP's
// rcp_regmap_general_t) with its Table 20 wire codec, the root-client/
// per-EP-restricted-client writer_ctx() derivation, the RC Server's own
// functional-configuration content (SvrEpCfg, TC18 §13.7.1.2), and the
// generic-vs-functional per-endpoint config split's *generic* half
// (EndpointGenericConfig, c-RCP's rcp_regmap_ep_generic_cfg_t) including its
// wire codec and the ep_delay_time/ep_req_storage_size boundary conversions.
//
// ── Phase 4 batch B rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17") ─────
// Ports the rest of c-RCP's own regmap.h/regmap.c batch A left out: HW pin
// mapping (HwPinMapEntry, replaced to match c-RCP's real
// rcp_regmap_hw_pin_map_entry_t row shape — its old pin_id/function
// placeholder had zero consumers anywhere in this codebase, confirmed
// before replacing it), the named-signal index (NamedSignal enum,
// named_signal_string()/named_signal_ep_signal_nr()), request-stream-cfg
// (RequestStreamConfig, EXTENDED — not replaced — with the fields c-RCP
// additionally carries: rx_secure_channel_index/rx_ack_stream_index/
// rx_resp_stream_index/rx_stream_max_request_size; its own pre-existing
// fields, read by e2e.hpp/watchdog.hpp/sim.hpp and their tests, are kept
// verbatim by name), its watchdog-timeout ms/ticks boundary conversions
// and TC18 0.5.1_RC5-reconciled wire codec (issue #458), response-queue-
// cfg (ResponseQueueConfig, replaced to match c-RCP's real per-queue row
// shape — its old response_queue_size/ack_queue_size/flush_time shape had
// zero real consumers outside this file; the only outside references,
// in respqueue.hpp's/deadline.hpp's own comments, already named fields —
// queue_size/max_avtpdu_size — that never existed on the old struct
// either, i.e. those comments were already stale, not a real dependency),
// the EP-ID/byte_bus_id map (EpIdMappingEntry, EXTENDED with
// request_stream_index/crc_required, appended as its LAST two fields so
// every existing 2-element positional call site — this file's own tests,
// rcp/mock.hpp's power-on EP-ID table — keeps compiling unchanged) plus
// its full diagnostic/query surface (is_ascending, effective_count,
// row_init_default, has_single_client_per_ep, shared_bus_homogeneous,
// ep_type_has_fixed_ep_id, byte_bus_ids_for_stream,
// is_valid_association), and the four optional-subsystem config sections
// (OptionalSubsystemCfg, REQ-RMAP-039).
//
// NOT ported in this batch: c-RCP's own EP0 address-routed dispatcher
// (rcp_regmap_ep0_decode_write_request()/_decode_read_request()/
// _encode_read_response(), regmap.h:3072-3549) — a single ~500-line
// cross-cutting orchestrator combining every sub-table's own wire codec
// with lifecycle-state/writer authorization and SEQUENCER_config's own
// ownership-aware access control, none of which this batch's own scope
// (ROADMAP.md Phase 4 batch B, this file's own header comment above)
// names — see optional_subsystem_cfg's own section comment below for the
// full rationale. rcp_regmap_sequencer_table_render()/_apply_reconfig()
// (REQ-SEQ-014) are likewise not ported for the same reason: they exist
// in c-RCP purely to serve that same dispatcher.
//
// Batch A's own two loose ends are closed out here:
//
//   1. writer_ctx()'s via_valid_stream_association was pinned fail-closed
//      (always false) pending a real EP-ID/byte_bus_id association check
//      — ep_id_map::is_valid_association() below is that check, now wired
//      in for real (see writer_ctx()'s own updated doc comment).
//
//   2. The cross-cutting collision batch A found and deliberately left
//      unresolved — c-RCP's own rcp_regmap_ep_generic_cfg_t has NO
//      per-role E2E CRC-enable fields (that content, in c-RCP, belongs to
//      the *functional* config's single ep_req_crc_enable field, not
//      three per-role ones), while this codebase's own pre-rewrite
//      EndpointGenericConfig already carries THREE — is now resolved, not
//      merely re-deferred: c-RCP's rcp_regmap_ep_functional_cfg_t is
//      ported for real below (EpFunctionalCfg, content-modeling only —
//      every concrete endpoint type in this project's own Phase 3 already
//      independently ported its 5 fields inline, per type, rather than
//      composing a shared base struct). EndpointGenericConfig's own three
//      CRC fields stay exactly where they are: they are not c-RCP's
//      ep_req_crc_enable under a different roof, they are a genuinely
//      different, broader mechanism this codebase built independently —
//      see EndpointGenericConfig's own updated doc comment for the full
//      reconciliation.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above (or,
// where explicitly cited via a REQ-RMAP-* tag, directly re-derived from
// c-RCP's own primary-source-verified field-by-field Table 20 citations); no
// text from that document is reproduced here. Full bit-for-bit register-map
// conformance against other TC18 implementations is not claimed — same
// disclaimer as rcp/avtp.hpp's/rcp/acf.hpp's own wire codecs.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace regmap {

// ── EndpointId / EP0 ──────────────────────────────────────────────────────────
// Identifies one configured endpoint slot in the register map. EP0 (below)
// is the reserved id for the RC Server's own pseudo-endpoint; real endpoints
// (GPIO, SPI, ...) are assigned ids starting at 1 by later milestones.
//
// EP0 is deliberately the same numeric value c-RCP's own
// RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID uses (see discovery.hpp's own
// kDiscoveryByteBusId) — discovery and the general register map are both
// reached through the same address (c-RCP regmap.h's own file header).

using EndpointId = uint16_t;
constexpr EndpointId kEp0 = 0;

// is_ep0 — REQ-RMAP-001, ported from c-RCP's rcp_regmap_is_ep0()
// (src/regmap.c). True iff ep_index is EP0.
constexpr bool is_ep0(EndpointId ep_index) noexcept { return ep_index == kEp0; }

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

// GeneralMapErrc — REQ-RMAP-024, ported from c-RCP's rcp_regmap_general_errc_t
// (include/rcp/regmap.h). Errors decoding a Table 20 general-register-map
// wire message (see GeneralMap's own "Table 20 wire codec" section below).
// This project's convention (Errc + std::error_category::message(), not a
// separate strerror() function) replaces c-RCP's rcp_regmap_general_strerror();
// the four-value error taxonomy itself is preserved unchanged.
enum class GeneralMapErrc : int {
    short_frame  = 1, // b/len shorter than the ACF_ABB fixed header or its declared payload length
    bad_msg_type = 2, // b is not an ACF_ABB message
    wrong_bus    = 3, // byte_bus_id is not EP0
    wrong_op     = 4, // op does not match the direction this function expects
};

inline const std::error_category& general_map_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.general"; }
        std::string message(int ev) const override {
            switch (static_cast<GeneralMapErrc>(ev)) {
            case GeneralMapErrc::short_frame:  return "rcp/regmap: frame too short";
            case GeneralMapErrc::bad_msg_type: return "rcp/regmap: unexpected ACF message type";
            case GeneralMapErrc::wrong_bus:    return "rcp/regmap: wrong byte_bus_id";
            case GeneralMapErrc::wrong_op:     return "rcp/regmap: wrong ACF op";
            default:                           return "rcp/regmap: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(GeneralMapErrc e) noexcept {
    return {static_cast<int>(e), general_map_category()};
}

// EpGenericCfgReconfigErrc — REQ-RMAP-079, ported from c-RCP's
// rcp_regmap_ep_generic_cfg_reconfig_errc_t (include/rcp/regmap.h). Errors
// applying an incoming write to an EndpointGenericConfig row (see
// ep_generic_cfg::apply_reconfig() below).
enum class EpGenericCfgReconfigErrc : int {
    short_write  = 1, // data_len == 0
    out_of_range = 2, // relative_start_address + data_len exceeds count * row length
};

inline const std::error_category& ep_generic_cfg_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.ep_generic_cfg_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<EpGenericCfgReconfigErrc>(ev)) {
            case EpGenericCfgReconfigErrc::short_write:
                return "rcp/regmap: ep_generic_cfg write has no data";
            case EpGenericCfgReconfigErrc::out_of_range:
                return "rcp/regmap: ep_generic_cfg write extends past the table's own current extent";
            default:
                return "rcp/regmap: ep_generic_cfg unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(EpGenericCfgReconfigErrc e) noexcept {
    return {static_cast<int>(e), ep_generic_cfg_reconfig_category()};
}

// ── Generic vs. functional endpoint config split ──────────────────────────────
// Every configured endpoint has two independent config blocks, deliberately
// not merged into one blob: the generic block is server-owned and
// endpoint-type-agnostic; the functional block is endpoint-type-specific.
// c-RCP's own file header (regmap.h) frames this the same way: "every
// concrete endpoint type ... composes rcp_regmap_ep_functional_cfg_t as its
// own first member", the generic block staying server-owned regardless.

// EndpointGenericConfig — REQ-RMAP-016, ported from c-RCP's
// rcp_regmap_ep_generic_cfg_t (include/rcp/regmap.h:1310-1418,
// src/regmap.c). Server-owned generic per-endpoint config: fields a
// client's functional configuration is never allowed to touch, however
// write-access to the functional block evolves as the server's lifecycle
// state changes (see Ep0::check_write_access below).
//
// Zero-initialization (every field 0/false) is this port's equivalent of
// c-RCP's rcp_regmap_ep_generic_cfg_init() — a separate init() free function
// is not needed here since C++ default member initializers already give
// every instance that same zeroed starting state.
struct EndpointGenericConfig {
    uint8_t  ep_type = 0;  // concrete meaning assigned by each endpoint type added in Phase 16/19; TC18 §13.2 Table 28/31 relative address 0x0000, 8 bit, R (read-only — see ep_generic_cfg::apply_reconfig() below)
    bool     ep_used = false; // TC18 §13.2 Table 28/31 relative address 0x0001 bit 0, R/W* — EP0's own row (index 0) is fixed to true; see apply_reconfig()'s own doc comment

    // ep_delay_time — TC18 §13.2 Table 28/31 relative address 0x0001.4:5, 2
    // bit, R/W*, restricted to exactly {1, 10, 20, 50} microseconds. This
    // field's own internal representation deliberately stays a free
    // microsecond value rather than the register's own 2-bit enum, since it
    // is consumed as a scheduling-tick unit by rcp/request.hpp, not by any
    // caller that needs to know the register's own packed encoding —
    // ep_delay_time_us_to_reg()/_reg_to_us() below are the boundary
    // conversion pair a wire codec needs (c-RCP issue #311 batch 2).
    uint32_t ep_delay_time = 0;

    // ep_req_storage_size — octets of request-payload storage reserved for
    // this endpoint. TC18's own register (relative 0x0002, 16 bit, R/W*) is
    // in 32-bit WORDS, not octets — this field stays octets internally (the
    // register's own maximum representable value, 65535 words, is 262140
    // octets, which would not fit a uint16_t octet count); see
    // ep_req_storage_size_words_to_octets()/_octets_to_words() below for the
    // boundary conversion.
    uint32_t ep_req_storage_size = 0;

    uint32_t ep_description    = 0; // TC18 §13.2 Table 28/31 relative address 0x0004, 32 bit, R/W*: user-defined description, no further structure given by TC18
    uint16_t ep_tx_buffer_size = 0; // TC18 §13.2 Table 28/31 relative address 0x0008, 16 bit, R/W*, 32-bit words (matches ep_req_storage_size's own unit, not octets); 0x0000 if the endpoint has no tx buffer
    uint16_t ep_rx_buffer_size = 0; // TC18 §13.2 Table 28/31 relative address 0x000A, 16 bit, R/W*, same shape as ep_tx_buffer_size for the endpoint's rx buffer

    // ── Pre-existing (pre-Phase-4), NOT c-RCP content — RECONCILED batch B ──
    // rcp/e2e.hpp's crc_required() and tests/test_e2e.cpp depend on these
    // three independently-settable per-role E2E CRC toggles (ROADMAP.md
    // milestone 50, v2.6.0). Batch A deferred reconciling them against
    // c-RCP's own rcp_regmap_ep_functional_cfg_t (which has a single
    // ep_req_crc_enable field, not three per-role ones) as out of its own
    // scope; batch B now closes that out, having ported
    // rcp_regmap_ep_functional_cfg_t for real (see EpFunctionalCfg below).
    //
    // RESOLUTION: these three fields stay here, unmoved — this is not a
    // deferral, it is the actual answer. They are NOT c-RCP's
    // ep_req_crc_enable under a different roof: c-RCP's field is
    // request-only and lives on the FUNCTIONAL config (one bool, TC18
    // §13.7 common entries); these three are a genuinely different,
    // broader, GENERIC-config mechanism this codebase built on top —
    // independently gating CRC verification for the request, the
    // acknowledge, AND the response, uniformly, for every endpoint type
    // regardless of whether that type's own functional config even has an
    // equivalent field. Nothing in this codebase conflates the two:
    // e2e.hpp's crc_required() reads only these three; every concrete
    // endpoint type added in this project's own Phase 3 (rcp/gpio.hpp,
    // rcp/adc.hpp, ...) already independently ported c-RCP's real
    // functional-config ep_req_crc_enable field into its OWN config
    // struct (e.g. gpio.hpp's own ep_req_crc_enable field, matching
    // EpFunctionalCfg::ep_req_crc_enable exactly) and reads only that
    // copy for its own wire encode/decode — see EpFunctionalCfg's own doc
    // comment below for why that per-type duplication, not composition
    // against a shared base, is this codebase's own chosen substitute for
    // c-RCP's "compose as first member" C idiom. The two mechanisms
    // operate at different layers and neither shadows the other.
    bool ep_req_crc_enable      = false;
    bool ep_ack_crc_enable      = false;
    bool ep_response_crc_enable = false;
};

// EpFunctionalCfg — REQ-RMAP-017, ported from c-RCP's
// rcp_regmap_ep_functional_cfg_t (include/rcp/regmap.h:1614-1627,
// src/regmap.c). c-RCP's own file header describes this as the
// functional-config prefix "common to every endpoint type. Every
// concrete endpoint type built in Phase 16/19 composes this struct as its
// own first member rather than re-declaring these five fields itself".
//
// NOT composed by anything in this file, and deliberately so: this
// project's own Phase 3 (this codebase's equivalent of c-RCP's Phase
// 16/19 — the point at which concrete endpoint types were added) already
// independently ported these exact 5 fields, inline, into each endpoint
// type's own functional-config struct (e.g. rcp/gpio.hpp's own
// ep_enable/ep_clear_req_storage/ep_req_crc_enable/ep_response_ts_enable/
// ep_suppress_response fields, rcp/adc.hpp's identical set) rather than
// composing a shared base type — a valid, if duplicative, substitute for
// c-RCP's "anonymous struct as first member" C idiom, which C++ has no
// equally zero-overhead equivalent of at this project's chosen level of
// genericity. Refactoring those already-shipped, already-tested Phase 3
// headers to compose EpFunctionalCfg instead is out of this batch's own
// scope (it would touch gpio.hpp/adc.hpp/can.hpp/i2c.hpp/iseled.hpp/
// lin.hpp/mdio.hpp/pwm.hpp/spi.hpp/uart.hpp and their own tests, none of
// which this batch otherwise touches).
//
// EpFunctionalCfg itself is therefore content-modeling only here — the
// same disposition SvrEpCfg above already has (also never wired into
// RegisterMap or Ep0) — not dead code, but not itself consumed by
// anything in this file either. RegisterMap::functional_configs stays
// EndpointFunctionalConfig (Ep0's own opaque-blob wire-level
// representation of "whatever bytes a functional config write carries"),
// entirely unaffected by this struct's addition.
//
// Zero-initialization (every flag false) is this port's equivalent of
// c-RCP's rcp_regmap_ep_functional_cfg_init().
struct EpFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false; // TC18's own register name renders this with
                                         // a single "s"; spelled correctly here as
                                         // this project's own identifier, not a wire
                                         // encoding (matches c-RCP's own field comment)
};

struct EndpointFunctionalConfig {
    std::vector<uint8_t> data; // endpoint-type-specific; interpreted starting at v2.3.0
};

// ── ep_generic_cfg boundary conversions, wire codec (c-RCP issue #311
//    batches 2-4; REQ-RMAP-076/077/078/079) ─────────────────────────────────
namespace ep_generic_cfg {

// ep_delay_time_us_to_reg — REQ-RMAP-076. Converts a microsecond value into
// TC18's own 2-bit register encoding (00b=1us, 01b=10us, 10b=20us,
// 11b=50us — Table 28/31's own definitive register definition). Returns
// false (leaving out_reg unchanged) for any microsecond value NOT exactly
// one of the 4 allowed ones — deliberately REJECTS rather than rounds,
// since this is a real R/W* configuration input, not a saturating-is-safe
// case (a silently-substituted delay would misconfigure the endpoint's own
// scheduling timing).
//
// NOTE: TC18's own separate prose (request_compound/_triggered/_chained's
// own field descriptions) instead reads "[1us, 20us, 20us, 50us]" — a
// duplicated 20us where the table's own 10us belongs, almost certainly a
// copy-paste typo in TC18's own text (c-RCP regmap.h's own investigation);
// the table, not the repeated prose, is treated as authoritative here.
inline bool ep_delay_time_us_to_reg(uint32_t delay_us, uint8_t& out_reg) noexcept {
    switch (delay_us) {
    case 1u:  out_reg = 0u; return true;
    case 10u: out_reg = 1u; return true;
    case 20u: out_reg = 2u; return true;
    case 50u: out_reg = 3u; return true;
    default:  return false;
    }
}

// ep_delay_time_reg_to_us — REQ-RMAP-076. The inverse conversion. reg is
// masked to its own 2 bits internally, so any input is well-defined and
// this can never fail — all 4 possible 2-bit register values are valid
// TC18 encodings with no reserved/undefined combination.
inline uint32_t ep_delay_time_reg_to_us(uint8_t reg) noexcept {
    static constexpr uint32_t kUsByReg[4] = {1u, 10u, 20u, 50u};
    return kUsByReg[reg & 0x3u];
}

// ep_req_storage_size_words_to_octets — REQ-RMAP-077. The read-side
// conversion: always exact, always fits uint32_t (max register value 65535
// words = 262140 octets), cannot fail.
constexpr uint32_t ep_req_storage_size_words_to_octets(uint16_t words) noexcept {
    return static_cast<uint32_t>(words) * 4u;
}

// ep_req_storage_size_octets_to_words — REQ-RMAP-077. The write-side
// inverse: octets -> words. Returns false (leaving out_words unchanged) if
// octets is not an exact multiple of 4 (no lossy rounding of a
// configuration input) or if the resulting word count would not fit the
// register's own 16-bit width.
inline bool ep_req_storage_size_octets_to_words(uint32_t octets, uint16_t& out_words) noexcept {
    if ((octets % 4u) != 0u) return false;
    const uint32_t words = octets / 4u;
    if (words > static_cast<uint32_t>(UINT16_MAX)) return false;
    out_words = static_cast<uint16_t>(words);
    return true;
}

// kRowLen — REQ-RMAP-078: each row's own 12-octet TC18-cited stride
// (relative address 12*N onward per endpoint N).
constexpr size_t kRowLen = 12;

// kMaxEntries is not itself a TC18-derived value (TC18 defines no fixed
// endpoint count) — matches every sibling table's own identical,
// not-spec-derived bound (c-RCP's RCP_REGMAP_*_MAX_ENTRIES family).
constexpr size_t kMaxEntries = 64;

// render — REQ-RMAP-078, ported from c-RCP's rcp_regmap_ep_generic_cfg_render()
// (src/regmap.c). Serializes entries[0..count) into out at each row's own
// 12-octet stride — out must have room for at least kRowLen*count octets.
//
// ep_type, ep_description, ep_tx_buffer_size, and ep_rx_buffer_size are
// serialized directly (already stored in their own wire-native unit or as a
// plain octet passthrough). ep_used (bit 0) and ep_delay_time (bits 4:5) are
// packed into the same octet (relative 0x0001); the reserved 3-bit/2-bit
// spans either side of ep_delay_time have no corresponding field and are
// left 0.
//
// DEFENSIVE FALLBACK (matches c-RCP exactly): if a row's internal
// ep_delay_time is not exactly one of TC18's 4 allowed values, this
// function does not fail or assert — it falls back to register value 0
// (1us, the shortest/smallest valid delay). Every freshly-default-
// constructed, not-yet-configured EndpointGenericConfig hits this fallback
// (ep_delay_time defaults to 0us, itself not one of the 4 allowed values)
// until something explicitly sets a valid value — consistent with a
// "never grant more delay than configured" bias. Likewise, an
// ep_req_storage_size that is not an exact multiple of 4, or whose word
// count exceeds the register's own 16-bit width, is clamped DOWN to the
// nearest representable word count (never rounded up).
inline void render(const EndpointGenericConfig* entries, size_t count, uint8_t* out) noexcept {
    for (size_t i = 0; i < count; ++i) {
        uint8_t delay_reg = 0;
        if (!ep_delay_time_us_to_reg(entries[i].ep_delay_time, delay_reg)) delay_reg = 0u;

        uint16_t req_storage_words = 0;
        if (!ep_req_storage_size_octets_to_words(entries[i].ep_req_storage_size, req_storage_words)) {
            uint32_t clamped = entries[i].ep_req_storage_size;
            if (clamped > 0xFFFFu * 4u) clamped = 0xFFFFu * 4u; // max representable octets
            req_storage_words = static_cast<uint16_t>(clamped / 4u); // floor: rounds down, never up
        }

        const uint8_t octet1 = static_cast<uint8_t>((entries[i].ep_used ? 0x01u : 0x00u) |
                                                      static_cast<uint8_t>((delay_reg & 0x3u) << 4));

        uint8_t* row = out + kRowLen * i;
        row[0] = entries[i].ep_type;
        row[1] = octet1;
        avtp::detail::put_u16(&row[2], req_storage_words);
        avtp::detail::put_u32(&row[4], entries[i].ep_description);
        avtp::detail::put_u16(&row[8], entries[i].ep_tx_buffer_size);
        avtp::detail::put_u16(&row[10], entries[i].ep_rx_buffer_size);
    }
}

// apply_reconfig — REQ-RMAP-079/087, ported from c-RCP's
// rcp_regmap_ep_generic_cfg_apply_reconfig() (src/regmap.c). Applies an
// incoming write of data[0..data_len) at relative_start_address to
// entries[0..count) — entries[i] is row i's own 12-octet stride, matching
// render()'s own layout.
//
// NOT the render()-then-patch-then-reparse-the-whole-buffer idiom c-RCP's
// own sibling tables use for THEIR apply_reconfig() — that idiom is safe
// only because their own render() is a lossless 1:1 round-trip. render()
// above is NOT lossless (its own defensive ep_delay_time fallback and
// ep_req_storage_size clamp): reparsing a whole rendered-then-patched row
// would silently "launder" any already-invalid field through its own
// fallback/clamp on every write, even for a field the write never touched
// at all. This function is therefore PER-FIELD, not per-buffer: each of the
// 5 writable fields in a row is updated ONLY if the write's own byte span
// FULLY covers that field's own octet range within the row — a write that
// only partially covers a multi-octet field leaves that field entirely
// unchanged.
//
// ep_type (relative 0x0000 within each row) is NEVER updated, regardless of
// whether the write's own byte span covers it: TC18 §13.7.1.2 states, in
// general terms, that writing to read-only registers "has no effect and
// request is confirmed normally" — ep_type is R, not R/W*, the one
// read-only field mixed into this otherwise fully-writable row. A write
// touching only ep_type's own byte still returns success (not an error).
//
// ep_used (bit 0 of octet 0x0001) has its own narrower, row-0-only override
// (REQ-RMAP-087) on top of its otherwise general R/W* status: TC18 Table 31
// states EP0's own bit is "fixed to 1 as EP0 needs to be always
// implemented". A write to row 0 that covers this bit never clears it —
// entries[0].ep_used is forced to true regardless of the incoming bit.
// Every other row honors the incoming bit normally; row 0's own
// ep_delay_time (bits 4:5 of the same octet) is unaffected by this override.
//
// Returns EpGenericCfgReconfigErrc::short_write if data_len is 0, or
// ::out_of_range if the write's own span exceeds count*kRowLen — entries is
// left entirely unchanged in either error case.
inline std::error_code apply_reconfig(EndpointGenericConfig* entries, size_t count,
                                       uint16_t relative_start_address,
                                       const uint8_t* data, size_t data_len) noexcept {
    if (data_len == 0u) return make_error_code(EpGenericCfgReconfigErrc::short_write);

    const size_t touched_start = relative_start_address;
    const size_t touched_end   = touched_start + data_len;
    if (touched_end > count * kRowLen) return make_error_code(EpGenericCfgReconfigErrc::out_of_range);

    const size_t row_start_idx = touched_start / kRowLen;
    const size_t row_end_idx   = (touched_end - 1u) / kRowLen; // inclusive

    for (size_t row_i = row_start_idx; row_i <= row_end_idx; ++row_i) {
        const size_t row_base = row_i * kRowLen;

        if (touched_start <= row_base + 1u && row_base + 1u + 1u <= touched_end) {
            const uint8_t octet1 = data[(row_base + 1u) - touched_start];

            if (row_i == 0u) {
                entries[row_i].ep_used = true; // forced -- TC18 Table 31: EP0's ep_used
                                                // bit is "fixed to 1", never clearable
            } else {
                entries[row_i].ep_used = (octet1 & 0x01u) != 0u;
            }
            entries[row_i].ep_delay_time =
                ep_delay_time_reg_to_us(static_cast<uint8_t>((octet1 >> 4) & 0x3u));
        }

        if (touched_start <= row_base + 2u && row_base + 2u + 2u <= touched_end) {
            const uint16_t words = avtp::detail::get_u16(&data[(row_base + 2u) - touched_start]);
            entries[row_i].ep_req_storage_size = ep_req_storage_size_words_to_octets(words);
        }

        if (touched_start <= row_base + 4u && row_base + 4u + 4u <= touched_end) {
            entries[row_i].ep_description = avtp::detail::get_u32(&data[(row_base + 4u) - touched_start]);
        }

        if (touched_start <= row_base + 8u && row_base + 8u + 2u <= touched_end) {
            entries[row_i].ep_tx_buffer_size = avtp::detail::get_u16(&data[(row_base + 8u) - touched_start]);
        }

        if (touched_start <= row_base + 10u && row_base + 10u + 2u <= touched_end) {
            entries[row_i].ep_rx_buffer_size = avtp::detail::get_u16(&data[(row_base + 10u) - touched_start]);
        }
    }

    return {};
}

} // namespace ep_generic_cfg

// ── HW pin-mapping config (extraction §3.7; TC18 §12.7.6 Tables 21/22) ───────
// HwPinMapEntry — REQ-RMAP-042/043, ported from c-RCP's
// rcp_regmap_hw_pin_map_entry_t (include/rcp/regmap.h:1685-1697,
// src/regmap.c). Replaces this codebase's own earlier, pre-rewrite
// pin_id/function placeholder shape: nothing outside this file or its own
// tests referenced either field (confirmed by search across this worktree
// before replacing them), so this is a clean replacement, not an
// additive extension the way EpIdMappingEntry/RequestStreamConfig below
// need to be for their own already-depended-upon fields.

// hw_pin_type bit layout (TC18 §12.7.6 Table 22, REQ-RMAP-042/043),
// primary-source-verified directly against the TC18 v0.5.1_RC PDF: four
// packed sub-fields, not a set of independent one-hot flags. Pull (bits
// 1:0): float/pull-down/pull-up — 0b11 is undefined by the table, left
// unnamed here rather than guessed. Output stage (bits 3:2):
// input/open-drain/open-source/push-pull — deliberately NOT a separate
// exclusive INPUT/OUTPUT flag pair: TC18's own text states "All outputs
// are always also an input" (REQ-RMAP-043), so a single 2-bit field
// selecting one of three OUTPUT drive modes (or plain input) is the only
// representation an output-is-simultaneously-readable-as-input pin can
// even have. Drive strength (bits 5:4): input/low/medium/high. Bit 6 is
// reserved, reads 0. Schmitt-Trigger (bit 7): a plain single bit.
namespace hw_pin {
constexpr uint8_t kPullMask  = 0x3u;
constexpr uint8_t kPullFloat = 0x0u;
constexpr uint8_t kPullDown  = 0x1u;
constexpr uint8_t kPullUp    = 0x2u;

constexpr uint8_t kStageMask       = 0xCu; // bits 3:2
constexpr uint8_t kStageInput      = 0x0u;
constexpr uint8_t kStageOpenDrain  = 0x4u;
constexpr uint8_t kStageOpenSource = 0x8u;
constexpr uint8_t kStagePushPull   = 0xCu;

constexpr uint8_t kDriveMask   = 0x30u; // bits 5:4
constexpr uint8_t kDriveInput  = 0x00u;
constexpr uint8_t kDriveLow    = 0x10u;
constexpr uint8_t kDriveMedium = 0x20u;
constexpr uint8_t kDriveHigh   = 0x30u;

// bit 6 reserved, reads 0 -- no constant; never set it.
constexpr uint8_t kSchmittTrigger = 1u << 7;
} // namespace hw_pin

struct HwPinMapEntry {
    uint8_t hw_ep_nr     = 0; // hardware endpoint number this pin belongs to
    uint8_t hw_ep_pin_nr = 0; // pin number within that endpoint
    uint8_t hw_pin_type  = 0; // hw_pin::k* bitmask above (TC18's own register
                               // name, Table 21) — REQ-RMAP-042
};

// HwPinMapReconfigErrc — REQ-RMAP-040/041, ported from c-RCP's
// rcp_regmap_hw_pin_map_reconfig_errc_t. Errors applying an incoming
// write to a HwPinMapEntry table (see hw_pin_map::apply_reconfig() below).
enum class HwPinMapReconfigErrc : int {
    short_write  = 1, // data_len == 0
    out_of_range = 2, // relative_start_address + data_len exceeds count * row length
};

inline const std::error_category& hw_pin_map_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.hw_pin_map_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<HwPinMapReconfigErrc>(ev)) {
            case HwPinMapReconfigErrc::short_write:
                return "rcp/regmap: HW_config write has no data";
            case HwPinMapReconfigErrc::out_of_range:
                return "rcp/regmap: HW_config write extends past the table's own current extent";
            default:
                return "rcp/regmap: HW_config unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(HwPinMapReconfigErrc e) noexcept {
    return {static_cast<int>(e), hw_pin_map_reconfig_category()};
}

// ── HW_config server-side storage + wire codec (REQ-RMAP-040/041) ────────────
// HW_config is a separate table pointed to by GeneralMap::svr_hw_cfg_ptr
// (an absolute address in the same EP0-scoped space Table 20 itself
// lives in, TC18 §12.7.6 issue #301 finding — see c-RCP's own regmap.h
// file-header investigation, ported into this port's own GeneralMap doc
// comment above).
namespace hw_pin_map {

// kRowLen — each row's own 3-octet TC18-cited stride (IO_Pin N at
// relative address 3*N/3*N+1/3*N+2).
constexpr size_t kRowLen = 3;

// kMaxEntries is not itself TC18-derived (svr_io_pin_count is a 16-bit
// register with no fixed upper bound) — matches every sibling table's
// own identical, not-spec-derived bound.
constexpr size_t kMaxEntries = 64;

// render — REQ-RMAP-040, ported from c-RCP's
// rcp_regmap_hw_pin_map_render(). Serializes entries[0..count) into out
// at each row's own 3-octet stride — out must have room for at least
// kRowLen*count octets.
inline void render(const HwPinMapEntry* entries, size_t count, uint8_t* out) noexcept {
    for (size_t i = 0; i < count; ++i) {
        out[kRowLen * i + 0] = entries[i].hw_ep_nr;
        out[kRowLen * i + 1] = entries[i].hw_ep_pin_nr;
        out[kRowLen * i + 2] = entries[i].hw_pin_type;
    }
}

// apply_reconfig — REQ-RMAP-041, ported from c-RCP's
// rcp_regmap_hw_pin_map_apply_reconfig(). Same "render current image,
// patch the addressed octets, re-parse the whole image back" idiom every
// pointed-to table in this file uses — safe here because render() above
// is a lossless 1:1 round-trip (unlike ep_generic_cfg's own render(),
// this table has no defensive fallback/clamp of its own). Every octet of
// every row is R/W* (TC18 Table 21 has no read-only sub-fields within a
// row), so no octet is ever silently skipped.
//
// Returns HwPinMapReconfigErrc::short_write if data_len is 0,
// ::out_of_range if count exceeds kMaxEntries (this port's own fixed
// scratch-buffer bound) or if the write's own span exceeds count*kRowLen
// — entries is left entirely unchanged in either error case.
inline std::error_code apply_reconfig(HwPinMapEntry* entries, size_t count,
                                       uint16_t relative_start_address,
                                       const uint8_t* data, size_t data_len) noexcept {
    if (data_len == 0u) return make_error_code(HwPinMapReconfigErrc::short_write);
    if (count > kMaxEntries) return make_error_code(HwPinMapReconfigErrc::out_of_range);

    const size_t block_len = count * kRowLen;
    if (static_cast<size_t>(relative_start_address) + data_len > block_len)
        return make_error_code(HwPinMapReconfigErrc::out_of_range);

    std::array<uint8_t, kMaxEntries * kRowLen> block{};
    render(entries, count, block.data());
    for (size_t i = 0; i < data_len; ++i) block[relative_start_address + i] = data[i];
    for (size_t i = 0; i < count; ++i) {
        entries[i].hw_ep_nr     = block[kRowLen * i + 0];
        entries[i].hw_ep_pin_nr = block[kRowLen * i + 1];
        entries[i].hw_pin_type  = block[kRowLen * i + 2];
    }

    return {};
}

} // namespace hw_pin_map

// ── Per-endpoint-type named-signal index (extraction §3.7; TC18 §12.7.6
//    Table 23) ─────────────────────────────────────────────────────────────
// NamedSignal — REQ-RMAP-044, ported from c-RCP's rcp_regmap_named_signal_t
// (include/rcp/regmap.h:1798-1906, src/regmap.c). The full named-signal
// index shared by every endpoint type, written once here and reused
// unmodified by every endpoint type. This is one flat enumeration for
// human-readable naming (named_signal_string()) — TC18 Table 23's own
// EP_Signal_Nr wire value is NOT this enum's own ordinal; it restarts at
// 0 for every endpoint type (REQ-RMAP-045). named_signal_ep_signal_nr()
// below is the converter between the two: this enum's flat ordinal (for
// naming/identity) and TC18's own per-type-relative wire value (for
// hw_ep_pin_nr, TC18 Table 21). Values are grouped by endpoint type, in
// the same order and per-type numbering Table 23 itself uses.
enum class NamedSignal : uint8_t {
    Gpio0 = 0, Gpio1, Gpio2, Gpio3, Gpio4, Gpio5, Gpio6, Gpio7,
    Gpio8, Gpio9, Gpio10, Gpio11, Gpio12, Gpio13, Gpio14, Gpio15,
    Gpio16, Gpio17, Gpio18, Gpio19, Gpio20, Gpio21, Gpio22, Gpio23,
    Gpio24, Gpio25, Gpio26, Gpio27, Gpio28, Gpio29, Gpio30, Gpio31,
    SpiClk, SpiPico, SpiPoci, SpiCs0, SpiCs1, SpiCs2, SpiCs3, SpiCs4, SpiCs5,
    I2cScl, I2cSda,
    // REQ-RMAP-044: TC18 Table 23 enumerates EP_Signal_Nr for every
    // endpoint type this codebase implements, not just GPIO/SPI/I2C.
    UartTx, UartRx, UartRts, UartCts,
    LinTxd, LinRxd, LinNslp,
    PwmOut,  // positive phase -- Table 23's own name
    PwmOutn, // inverted phase -- Table 23's own name
    PwmIn,
    AdcIn,
    DacOut,
    CanRxd,
    CanTxd, // TC18's own counter-intuitive order: RXD=0, TXD=1 (Table 23)
    IseledIspP, IseledIspN,
    MdioMdc,
    MdioData, // Table 23 names this signal "MDIO" itself, identical to the
              // endpoint type name -- disambiguated here as _DATA to avoid
              // a name collision with the type name; not a departure from
              // the wire meaning
    Count, // not itself a valid signal; the number of named signals defined above
};

// named_signal_string — REQ-RMAP-014/015/082, ported from c-RCP's
// rcp_regmap_named_signal_string(). Human-readable, unique name for sig.
// Returns "unknown" (never a null/empty distinguishing failure) for a
// value outside 0..Count-1.
inline const char* named_signal_string(NamedSignal sig) noexcept {
    switch (sig) {
    case NamedSignal::Gpio0:  return "GPIO0";
    case NamedSignal::Gpio1:  return "GPIO1";
    case NamedSignal::Gpio2:  return "GPIO2";
    case NamedSignal::Gpio3:  return "GPIO3";
    case NamedSignal::Gpio4:  return "GPIO4";
    case NamedSignal::Gpio5:  return "GPIO5";
    case NamedSignal::Gpio6:  return "GPIO6";
    case NamedSignal::Gpio7:  return "GPIO7";
    case NamedSignal::Gpio8:  return "GPIO8";
    case NamedSignal::Gpio9:  return "GPIO9";
    case NamedSignal::Gpio10: return "GPIO10";
    case NamedSignal::Gpio11: return "GPIO11";
    case NamedSignal::Gpio12: return "GPIO12";
    case NamedSignal::Gpio13: return "GPIO13";
    case NamedSignal::Gpio14: return "GPIO14";
    case NamedSignal::Gpio15: return "GPIO15";
    case NamedSignal::Gpio16: return "GPIO16";
    case NamedSignal::Gpio17: return "GPIO17";
    case NamedSignal::Gpio18: return "GPIO18";
    case NamedSignal::Gpio19: return "GPIO19";
    case NamedSignal::Gpio20: return "GPIO20";
    case NamedSignal::Gpio21: return "GPIO21";
    case NamedSignal::Gpio22: return "GPIO22";
    case NamedSignal::Gpio23: return "GPIO23";
    case NamedSignal::Gpio24: return "GPIO24";
    case NamedSignal::Gpio25: return "GPIO25";
    case NamedSignal::Gpio26: return "GPIO26";
    case NamedSignal::Gpio27: return "GPIO27";
    case NamedSignal::Gpio28: return "GPIO28";
    case NamedSignal::Gpio29: return "GPIO29";
    case NamedSignal::Gpio30: return "GPIO30";
    case NamedSignal::Gpio31: return "GPIO31";
    case NamedSignal::SpiClk:  return "SPI_CLK";
    case NamedSignal::SpiPico: return "SPI_PICO";
    case NamedSignal::SpiPoci: return "SPI_POCI";
    case NamedSignal::SpiCs0:  return "SPI_CS0";
    case NamedSignal::SpiCs1:  return "SPI_CS1";
    case NamedSignal::SpiCs2:  return "SPI_CS2";
    case NamedSignal::SpiCs3:  return "SPI_CS3";
    case NamedSignal::SpiCs4:  return "SPI_CS4";
    case NamedSignal::SpiCs5:  return "SPI_CS5";
    case NamedSignal::I2cScl:  return "I2C_SCL";
    case NamedSignal::I2cSda:  return "I2C_SDA";
    case NamedSignal::UartTx:  return "UART_TX";
    case NamedSignal::UartRx:  return "UART_RX";
    case NamedSignal::UartRts: return "UART_RTS";
    case NamedSignal::UartCts: return "UART_CTS";
    case NamedSignal::LinTxd:  return "LIN_TXD";
    case NamedSignal::LinRxd:  return "LIN_RXD";
    case NamedSignal::LinNslp: return "LIN_NSLP";
    case NamedSignal::PwmOut:  return "PWM_OUT";
    case NamedSignal::PwmOutn: return "PWM_OUTN";
    case NamedSignal::PwmIn:   return "PWM_IN";
    case NamedSignal::AdcIn:   return "ADC_IN";
    case NamedSignal::DacOut:  return "DAC_OUT";
    case NamedSignal::CanRxd:  return "CAN_RXD";
    case NamedSignal::CanTxd:  return "CAN_TXD";
    case NamedSignal::IseledIspP: return "ISELED_ISP_P";
    case NamedSignal::IseledIspN: return "ISELED_ISP_N";
    case NamedSignal::MdioMdc:  return "MDIO_MDC";
    case NamedSignal::MdioData: return "MDIO_DATA";
    default: return "unknown";
    }
}

// named_signal_ep_signal_nr — REQ-RMAP-045, ported from c-RCP's
// rcp_regmap_named_signal_ep_signal_nr(). Converts sig's own flat enum
// ordinal into TC18 Table 23's per-endpoint-type EP_Signal_Nr wire value
// — the value hw_ep_pin_nr (Table 21) actually carries, which restarts
// at 0 for every endpoint type rather than continuing this enum's own
// flat numbering. Returns 0 for NamedSignal::Count or any other value
// outside 0..Count-1 (there is no meaningful EP_Signal_Nr for a signal
// that doesn't exist; 0 is chosen over an out-of-band sentinel to keep
// the return type a plain uint8_t, matching the wire field's own width —
// callers that need to distinguish "not a real signal" from "really is
// EP_Signal_Nr 0" should validate sig against NamedSignal::Count
// themselves before calling).
inline uint8_t named_signal_ep_signal_nr(NamedSignal sig) noexcept {
    const auto raw = static_cast<uint8_t>(sig);
    // NamedSignal is uint8_t-backed and Gpio0 == 0, so raw can never be
    // "negative" -- no explicit lower-bound check is needed the way
    // c-RCP's own signed-parameter version needs one.
    if (raw <= static_cast<uint8_t>(NamedSignal::Gpio31)) return raw; // GPIOn's own per-type number is n
    if (raw <= static_cast<uint8_t>(NamedSignal::SpiCs5))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::SpiClk));
    if (raw <= static_cast<uint8_t>(NamedSignal::I2cSda))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::I2cScl));
    if (raw <= static_cast<uint8_t>(NamedSignal::UartCts))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::UartTx));
    if (raw <= static_cast<uint8_t>(NamedSignal::LinNslp))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::LinTxd));
    if (raw <= static_cast<uint8_t>(NamedSignal::PwmOutn))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::PwmOut));
    if (sig == NamedSignal::PwmIn) return 0u;
    if (sig == NamedSignal::AdcIn) return 0u;
    if (sig == NamedSignal::DacOut) return 0u;
    if (raw <= static_cast<uint8_t>(NamedSignal::CanTxd))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::CanRxd));
    if (raw <= static_cast<uint8_t>(NamedSignal::IseledIspN))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::IseledIspP));
    if (raw <= static_cast<uint8_t>(NamedSignal::MdioData))
        return static_cast<uint8_t>(raw - static_cast<uint8_t>(NamedSignal::MdioMdc));
    return 0u; // NamedSignal::Count or any other invalid value
}

// ── Sequencer-state registers (extraction §3.11, §3.16) ──────────────────────
// Persistent 8-bit values; behavior lives in rcp/request.hpp (v2.5.0).
// Reviewed against c-RCP's own Seq_state register (TC18 §12.7.10 Table 28)
// as part of batch B and left unchanged: this already matches its meaning
// exactly (a plain persistent 8-bit value). c-RCP's own SEQUENCER_config
// WIRE codec (rcp_regmap_sequencer_table_render()/_apply_reconfig(),
// REQ-SEQ-014) is not ported — it exists in c-RCP purely to serve the EP0
// address-routed dispatcher, itself out of this batch's scope (see this
// file's own "Phase 4 batch B" banner above).

using SequencerState = uint8_t;

// ── rx_safety_measure selector (extraction §3.8) ──────────────────────────────
// Reviewed against c-RCP's own rx_safety_measure field as part of batch B
// and left unchanged: already matches c-RCP's RCP_E2E_MEASURE_FORCE_HIGH_
// IMPEDANCE/RCP_E2E_MEASURE_SEQUENCER pair exactly, content-modeling only
// on both sides (neither this enum's value nor c-RCP's own has a wire
// register position of its own — TC18 0.5.1_RC5 has no 1:1 replacement
// for it, see request_stream_cfg's own file-header note below).

enum class RxSafetyMeasure : uint8_t {
    ForceHighImpedance = 0, // hold outputs high-impedance; no sequencer consulted
    RunSafeSequencer   = 1, // "safe" is rx_safestate_sequencer reading rx_safe_sequencer_state
};

// ── Request-stream config (extraction §3.8; TC18 §12.7.7 Table 24) ──────────
// RequestStreamConfig — REQ-RMAP-018, extended from this codebase's own
// pre-rewrite v2.x design (rcp/e2e.hpp, rcp/watchdog.hpp, rcp/sim.hpp, and
// their own tests already depend on the fields batch A/pre-rewrite already
// had — stream_id, queue_size, rx_wd_timeout_interval, rx_wd_enable,
// rx_wd_safestate_enable, rx_wd_info_enable, rx_enforce_e2e, rx_enforce_seq,
// rx_seq_safestate_enable, rx_ovrflw_safestate_enable, rx_safety_measure,
// rx_safestate_sequencer, rx_safe_sequencer_state — every one of those kept
// verbatim, by name, below). This batch APPENDS the fields c-RCP's own
// rcp_regmap_request_stream_cfg_t (include/rcp/regmap.h:1920-2187,
// src/regmap.c) additionally carries that nothing in this codebase yet
// modeled: rx_secure_channel_index/rx_ack_stream_index/rx_resp_stream_index
// (REQ-RMAP-047/048/049) and rx_stream_max_request_size (fragmentation,
// c-RCP's own Phase 20 addition). rx_wd_timeout_interval is this codebase's
// own pre-existing name for what c-RCP calls rx_wd_timeout_ms — same
// milliseconds unit, not renamed here since e2e.hpp/watchdog.hpp/sim.hpp
// and their tests all already depend on the existing name.
//
// stream_id (avtp::StreamId, not a raw uint64_t rx_stream_id) and
// queue_size (present in this codebase's pre-rewrite design, but with no
// counterpart anywhere in c-RCP's own Table 24 — confirmed by direct
// primary-source read before leaving it alone) are both kept exactly as
// they already were: stream_id because it's this codebase's own
// established stream-identity representation (used the same way
// GeneralMap and every other wire codec in this file use avtp::StreamId/
// ByteBusId rather than a bare integer), queue_size because nothing
// outside this file/its own tests reads or writes it and removing an
// unused, harmless, pre-existing field is not this batch's job.
struct RequestStreamConfig {
    avtp::StreamId stream_id{};
    uint16_t       queue_size = 0;

    uint32_t rx_wd_timeout_interval = 0;
    bool     rx_wd_enable           = false;
    bool     rx_wd_safestate_enable = false; // watchdog overflow drives the endpoint into safe state
    bool     rx_wd_info_enable      = false; // repeating notification while latched in safe state

    bool rx_enforce_e2e = false;

    bool rx_enforce_seq          = false;
    bool rx_seq_safestate_enable = false;

    bool rx_ovrflw_safestate_enable = false;

    RxSafetyMeasure rx_safety_measure      = RxSafetyMeasure::ForceHighImpedance;
    uint16_t        rx_safestate_sequencer = 0;
    SequencerState  rx_safe_sequencer_state = 0;

    // ── Secure channel / acknowledge & response routing (new this batch) ──
    uint8_t rx_secure_channel_index = 0; // REQ-RMAP-047 (Table 24, relative 0x000C, 8 bit,
                                          // R/W*): which secure channel this request stream
                                          // is carried on. 0 is TC18's own defined "no cyber
                                          // security, MACsec uncontrolled port" encoding --
                                          // content modeling only, this codebase has no
                                          // MACsec layer of its own to select one in.
    uint8_t rx_ack_stream_index     = 0; // REQ-RMAP-048 (Table 24, relative 0x0010, 8 bit,
                                          // R/W*): index of the response/ack stream (see
                                          // ResponseQueueConfig below) endpoints bound to
                                          // this request stream send their acknowledges on.
                                          // 0 is TC18's own "no acknowledge is to be sent"
                                          // encoding -- content modeling only.
    uint8_t rx_resp_stream_index    = 1; // REQ-RMAP-049 (Table 24, relative 0x0011, 8 bit,
                                          // R/W*): index of the response stream. 0 is TC18's
                                          // own "no response is to be sent" encoding; the
                                          // power-on default is 1, not 0, so a freshly reset
                                          // server can answer a discovery request before any
                                          // configuration has been written (matches c-RCP's
                                          // own rcp_regmap_request_stream_cfg_init()).

    // ── Fragmentation (new this batch) ─────────────────────────────────────
    // rx_stream_max_request_size -- the largest single-AVTPDU ACF payload
    // (header-and-payload, excluding any e2e.hpp CRC trailer) this stream
    // will assemble or accept in one fragment. 0 means fragmentation is
    // unsupported for this stream. size_t internally (matching a byte-count
    // convention), saturated -- never wrapped -- to the wire's 16-bit width
    // by request_stream_cfg::render() below.
    size_t rx_stream_max_request_size = 0;
};

// ── request-stream-cfg boundary conversions, wire codec (issue #306,
//    REQ-RMAP-047/048/049/050/071; TC18 0.5.1_RC5 reconciliation, issue
//    #458) ───────────────────────────────────────────────────────────────
//
// TC18 0.5.1_RC5 terminology drift (ported from c-RCP's own investigation,
// regmap.h file header, "issue #458"): row-relative octet 0x000D's real
// RC5 layout is 4 meaningful bits (bit0 rx_enforce_crc, bit1
// rx_enforce_sequence, bit2 rx_enforce_watchdog, bit3
// rx_enforce_request_filing; bits [6:4] Reserved, R only; bit7
// rx_stream_status), not 8 independently-configurable bits. This
// codebase's own richer model (rx_enforce_e2e; rx_enforce_seq +
// rx_seq_safestate_enable; rx_wd_enable + rx_wd_safestate_enable;
// rx_ovrflw_safestate_enable) deliberately keeps the "block" and "also
// enter safe state" dimensions of the sequence/watchdog pairs
// independently expressible internally (e2e.hpp's own design) -- a strict
// superset of what an RC5 wire peer can express -- but render() below
// renders each of those two combined wire bits true ONLY when BOTH
// internal dimensions agree (AND, never OR), since OR would let a stream
// that only blocks (without entering safe state) falsely claim, to a real
// RC5 peer reading this register, that it also enters safe state -- an
// overstated safety guarantee this port must never produce. The reverse
// direction is exact and lossless: a real RC5 write can only ever express
// the coupled state to begin with, so apply_reconfig() sets BOTH internal
// dimensions of a pair together from that one arriving bit.
// rx_safety_measure and rx_wd_info_enable both have NO 1:1 replacement in
// RC5's real 4-bit scheme (c-RCP's own still-open finding) -- both are
// content-modeling only, with no wire register position of their own,
// same disposition rx_wd_action has in c-RCP (this port has no
// counterpart field for rx_wd_action at all: c-RCP's own primary-source
// verification found no corresponding register for it anywhere in TC18).
namespace request_stream_cfg {

// wd_timeout_ms_to_ticks — REQ-RMAP-050/083, ported from c-RCP's
// rcp_regmap_wd_timeout_ms_to_ticks(). TC18 names no fixed clock-tick
// rate for rx_wd_timeout_interval's own register anywhere near its own
// definition, so (matching this codebase's own caller-supplies-already-
// classified-units convention) the caller supplies ms_per_tick. Rounds
// DOWN (never grants more slack than requested -- a safety-integrity
// register should never silently widen a configured watchdog period).
// Returns false (leaving out_ticks unchanged) if ms_per_tick == 0 (no
// register value for a zero-length tick) or if the converted tick count
// would not fit the register's own 16-bit width.
inline bool wd_timeout_ms_to_ticks(uint32_t timeout_ms, uint32_t ms_per_tick,
                                   uint16_t& out_ticks) noexcept {
    if (ms_per_tick == 0u) return false;
    const uint32_t ticks = timeout_ms / ms_per_tick;
    if (ticks > static_cast<uint32_t>(UINT16_MAX)) return false;
    out_ticks = static_cast<uint16_t>(ticks);
    return true;
}

// wd_timeout_ticks_to_ms — REQ-RMAP-050, the inverse conversion. Returns
// false (leaving out_timeout_ms unchanged) if ms_per_tick == 0 or if the
// product would overflow uint32_t.
inline bool wd_timeout_ticks_to_ms(uint16_t ticks, uint32_t ms_per_tick,
                                   uint32_t& out_timeout_ms) noexcept {
    if (ms_per_tick == 0u) return false;
    const uint64_t product = static_cast<uint64_t>(ticks) * static_cast<uint64_t>(ms_per_tick);
    if (product > static_cast<uint64_t>(UINT32_MAX)) return false;
    out_timeout_ms = static_cast<uint32_t>(product);
    return true;
}

// kRowLen — REQ-RMAP-047: TC18's own 24-octet-per-request-stream wire
// stride (confirmed via direct primary-source read, both spec revisions).
constexpr size_t kRowLen = 24;

// kMaxEntries is not itself TC18-derived -- matches every sibling table's
// own identical, not-spec-derived bound.
constexpr size_t kMaxEntries = 64;

// render — REQ-RMAP-047/048/049/050/051/071, ported from c-RCP's
// rcp_regmap_request_stream_cfg_render(). Serializes entries[0..count)
// into out at each row's own 24-octet stride. watchdog_ms_per_tick is
// rx_wd_timeout_interval's own caller-supplied tick duration (see
// wd_timeout_ms_to_ticks() above) -- a value that cannot be represented
// at that rate (including watchdog_ms_per_tick == 0, "not configured")
// falls back to encoding 0x0000, the same "reserved / cannot be
// represented, use 0" treatment this file already uses elsewhere.
// rx_stream_max_request_size and rx_safestate_sequencer are each
// saturated (never wrapped) to their own narrower wire widths (16 bit,
// 8 bit respectively) since wraparound would silently alias onto ANOTHER
// valid, meaningfully-different value.
//
// rx_stream_status_blocked (issue #424, REQ-E2E-046/REQ-RMAP-051) is a
// caller-supplied, index-parallel array (entries[i] <->
// rx_stream_status_blocked[i]) of already-computed aggregate values for
// TC18's own distinct, live rx_stream_status bit (row-relative 0x000D bit
// 7) -- may be nullptr, meaning "no live status known" (bit 7 then
// renders 0), matching this file's other NULL-means-absent optional
// inputs.
inline void render(const RequestStreamConfig* entries, size_t count, uint8_t* out,
                    uint32_t watchdog_ms_per_tick, const bool* rx_stream_status_blocked) noexcept {
    for (size_t i = 0; i < count; ++i) {
        const RequestStreamConfig& e = entries[i];
        uint8_t* row = out + kRowLen * i;

        avtp::detail::put_u64(&row[0x0000], e.stream_id.to_u64());

        const uint16_t max_request_size_wire =
            (e.rx_stream_max_request_size > 0xFFFFu) ? uint16_t{0xFFFFu}
                                                      : static_cast<uint16_t>(e.rx_stream_max_request_size);
        avtp::detail::put_u16(&row[0x0008], max_request_size_wire);

        uint16_t wd_timeout_ticks = 0;
        if (wd_timeout_ms_to_ticks(e.rx_wd_timeout_interval, watchdog_ms_per_tick, wd_timeout_ticks)) {
            avtp::detail::put_u16(&row[0x000A], wd_timeout_ticks);
        } else {
            row[0x000A] = 0x00;
            row[0x000B] = 0x00;
        }

        row[0x000C] = e.rx_secure_channel_index;

        uint8_t bits_0x000d = static_cast<uint8_t>(e.rx_enforce_e2e ? 0x01u : 0x00u);
        bits_0x000d = static_cast<uint8_t>(
            bits_0x000d | ((e.rx_enforce_seq && e.rx_seq_safestate_enable) ? 0x02u : 0x00u));
        bits_0x000d = static_cast<uint8_t>(
            bits_0x000d | ((e.rx_wd_enable && e.rx_wd_safestate_enable) ? 0x04u : 0x00u));
        bits_0x000d = static_cast<uint8_t>(bits_0x000d | (e.rx_ovrflw_safestate_enable ? 0x08u : 0x00u));
        bits_0x000d = static_cast<uint8_t>(
            bits_0x000d |
            ((rx_stream_status_blocked != nullptr && rx_stream_status_blocked[i]) ? 0x80u : 0x00u));
        row[0x000D] = bits_0x000d;

        const uint8_t safestate_sequencer_wire = (e.rx_safestate_sequencer > 0xFFu)
                                                      ? uint8_t{0xFFu}
                                                      : static_cast<uint8_t>(e.rx_safestate_sequencer);
        row[0x000E] = safestate_sequencer_wire;
        row[0x000F] = e.rx_safe_sequencer_state;
        row[0x0010] = e.rx_ack_stream_index;
        row[0x0011] = e.rx_resp_stream_index;

        row[0x0012] = 0x00; row[0x0013] = 0x00; // reserved (16 bit)
        row[0x0014] = 0x00; row[0x0015] = 0x00; // reserved (32 bit)
        row[0x0016] = 0x00; row[0x0017] = 0x00;
    }
}

} // namespace request_stream_cfg

// RequestStreamCfgReconfigErrc — REQ-RMAP-047/048/049/071, ported from
// c-RCP's rcp_regmap_request_stream_cfg_reconfig_errc_t.
enum class RequestStreamCfgReconfigErrc : int {
    short_write  = 1,
    out_of_range = 2,
};

inline const std::error_category& request_stream_cfg_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.request_stream_cfg_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<RequestStreamCfgReconfigErrc>(ev)) {
            case RequestStreamCfgReconfigErrc::short_write:
                return "rcp/regmap: request-stream-cfg write has no data";
            case RequestStreamCfgReconfigErrc::out_of_range:
                return "rcp/regmap: request-stream-cfg write extends past the table's own current extent";
            default:
                return "rcp/regmap: request-stream-cfg unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(RequestStreamCfgReconfigErrc e) noexcept {
    return {static_cast<int>(e), request_stream_cfg_reconfig_category()};
}

namespace request_stream_cfg {

// apply_reconfig — REQ-RMAP-047/048/049/050/071, ported from c-RCP's
// rcp_regmap_request_stream_cfg_apply_reconfig(). Same render-patch-
// reparse idiom as every other pointed-to table's own apply_reconfig()
// in this file. A write landing on the 3 reserved trailing octets, on
// bits [6:4] of 0x000D (Reserved, R only), or on bit 7 of 0x000D
// (rx_stream_status, a live server-computed status, not client-
// configurable content) is accepted (every octet here is R/W or R/W*,
// nothing this codec itself must reject) but has no effect on any struct
// field -- it patches the transient scratch image this function builds
// internally, discarded rather than re-parsed back into anything for
// those specific bits. bits [3:0] of 0x000D remain the genuinely R/W*
// content-bearing fields this function round-trips: bit1/bit2 each set
// BOTH of this codebase's own two internal dimensions of their pair
// together, since a real RC5 write can only ever express the coupled
// "block AND enter safe state" state (see this table's own file-header
// note above). rx_safety_measure and rx_wd_info_enable are left
// unchanged (no wire register position of their own).
inline std::error_code apply_reconfig(RequestStreamConfig* entries, size_t count,
                                       uint16_t relative_start_address, const uint8_t* data,
                                       size_t data_len, uint32_t watchdog_ms_per_tick) noexcept {
    if (data_len == 0u) return make_error_code(RequestStreamCfgReconfigErrc::short_write);
    if (count > kMaxEntries) return make_error_code(RequestStreamCfgReconfigErrc::out_of_range);

    const size_t block_len = count * kRowLen;
    if (static_cast<size_t>(relative_start_address) + data_len > block_len)
        return make_error_code(RequestStreamCfgReconfigErrc::out_of_range);

    std::array<uint8_t, kMaxEntries * kRowLen> block{};
    render(entries, count, block.data(), watchdog_ms_per_tick, nullptr);
    for (size_t i = 0; i < data_len; ++i) block[relative_start_address + i] = data[i];

    for (size_t i = 0; i < count; ++i) {
        RequestStreamConfig& e = entries[i];
        const uint8_t*       row = &block[kRowLen * i];

        e.stream_id               = avtp::StreamId::from_u64(avtp::detail::get_u64(&row[0x0000]));
        e.rx_stream_max_request_size = static_cast<size_t>(avtp::detail::get_u16(&row[0x0008]));

        uint32_t wd_timeout_ms = 0;
        if (wd_timeout_ticks_to_ms(avtp::detail::get_u16(&row[0x000A]), watchdog_ms_per_tick,
                                    wd_timeout_ms)) {
            e.rx_wd_timeout_interval = wd_timeout_ms;
        }
        e.rx_secure_channel_index = row[0x000C];

        const uint8_t bits_0x000d = row[0x000D];
        e.rx_enforce_e2e          = (bits_0x000d & 0x01u) != 0u;
        {
            const bool seq_bit          = (bits_0x000d & 0x02u) != 0u;
            e.rx_enforce_seq          = seq_bit;
            e.rx_seq_safestate_enable = seq_bit;
        }
        {
            const bool wd_bit          = (bits_0x000d & 0x04u) != 0u;
            e.rx_wd_enable            = wd_bit;
            e.rx_wd_safestate_enable  = wd_bit;
        }
        e.rx_ovrflw_safestate_enable = (bits_0x000d & 0x08u) != 0u;
        // bit 7 (rx_stream_status) intentionally not unpacked into any
        // field -- see this function's own doc comment above.

        e.rx_safestate_sequencer  = static_cast<uint16_t>(row[0x000E]);
        e.rx_safe_sequencer_state = row[0x000F];
        e.rx_ack_stream_index     = row[0x0010];
        e.rx_resp_stream_index    = row[0x0011];
    }

    return {};
}

// resolve_index — REQ-SEQ-013 (issue #335), ported from c-RCP's
// rcp_regmap_request_stream_cfg_resolve_index(). Resolves stream_id to
// its own 1-based position in entries[0..count) (0 reserved as a "no
// match" sentinel rather than a real index, matching
// EpIdMappingEntry.request_stream_index's own convention below). Returns
// 0 if no entry's own stream_id equals stream_id, or entries is nullptr.
inline uint8_t resolve_index(const RequestStreamConfig* entries, size_t count,
                              uint64_t stream_id) noexcept {
    if (entries == nullptr) return 0u;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].stream_id.to_u64() == stream_id) return static_cast<uint8_t>(i + 1u);
    }
    return 0u;
}

} // namespace request_stream_cfg

// ── EP-ID / byte_bus_id mapping table (extraction §3.9; TC18 §12.7.8
//    Table 25/26) ─────────────────────────────────────────────────────────
// EpIdMappingEntry — REQ-RMAP-052/053, extended from this codebase's own
// pre-rewrite v2.x design (ep_id, byte_bus_id kept verbatim; nothing
// outside this file/its own tests reads or writes those two fields under
// their pre-existing 2-field shape, confirmed before extending). c-RCP's
// own rcp_regmap_ep_id_map_entry_t (include/rcp/regmap.h:2577-2661,
// src/regmap.c) additionally carries request_stream_index and
// crc_required -- appended here, in that order, as this struct's LAST two
// fields (matching c-RCP's own reason for that placement: existing
// 2-element brace-init call sites, e.g. this file's own tests and
// rcp/mock.hpp's power-on EP-ID table, keep compiling unchanged, with
// both new fields correctly defaulting).
struct EpIdMappingEntry {
    EndpointId      ep_id       = 0;
    avtp::ByteBusId byte_bus_id = 0;

    uint8_t request_stream_index = 0; // REQ-RMAP-052 (Table 25, row offset 0x0000, 8 bit,
                                       // R/W+): which request stream this row's mapping
                                       // applies to. 0 is TC18's own defined end-of-table
                                       // sentinel (REQ-RMAP-054) -- see
                                       // ep_id_map::effective_count() below. This is what
                                       // is_valid_association() below needs to answer
                                       // TC18 §12.3.1.2's "any valid stream_id/byte_bus_id
                                       // combination" writer_ctx() question for real.
    bool    crc_required         = false; // REQ-RMAP-053 (issue #421, Table 25 row offset
                                       // 0x0002 bit 4 "Ctrl.CRC_required"). Channel_selection
                                       // (Table 26 bits [3:0]) is deliberately NOT modeled --
                                       // always renders 0, silently dropped on write, per
                                       // c-RCP's own dedicated investigation (still-draft
                                       // BBID-based proposal; evt-bits remain authoritative).
};

// EpIdMapReconfigErrc — REQ-RMAP-052/053/054, ported from c-RCP's
// rcp_regmap_ep_id_map_reconfig_errc_t.
enum class EpIdMapReconfigErrc : int {
    short_write  = 1,
    out_of_range = 2,
};

inline const std::error_category& ep_id_map_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.ep_id_map_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<EpIdMapReconfigErrc>(ev)) {
            case EpIdMapReconfigErrc::short_write:
                return "rcp/regmap: EP_ID_config write has no data";
            case EpIdMapReconfigErrc::out_of_range:
                return "rcp/regmap: EP_ID_config write extends past the table's own current extent";
            default:
                return "rcp/regmap: EP_ID_config unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(EpIdMapReconfigErrc e) noexcept {
    return {static_cast<int>(e), ep_id_map_reconfig_category()};
}

namespace ep_id_map {

// kRowLen — REQ-RMAP-052: TC18's own 4-octet-per-row stride
// (request_stream_index @0x0000, ep_id/EP_Nr @0x0001, byte_bus_id/Ctrl
// @0x0002, confirmed directly against the primary source).
constexpr size_t kRowLen = 4;

// kMaxEntries is not itself TC18-derived -- matches every sibling table's
// own identical, not-spec-derived bound.
constexpr size_t kMaxEntries = 64;

// is_ascending — REQ-RMAP-056, ported from c-RCP's
// rcp_regmap_ep_id_map_is_ascending(). Read-only diagnostic: true iff
// entries[0..count) is strictly ascending in the COMPOSITE key
// (request_stream_index, byte_bus_id) -- request_stream_index must never
// decrease, and within one unchanged request_stream_index run,
// byte_bus_id must strictly increase. A higher request_stream_index
// always counts as ascending regardless of that row's own byte_bus_id
// value. Vacuously true for count < 2.
//
// NOTE (ported from c-RCP's own file-header, "Known spec ambiguity"):
// this ordering has NO server-side enforcement in the specification
// itself (as of TC18 0.5.1_RC4 the requirement was deleted from the spec
// entirely) -- this function is a read-only diagnostic for tooling, never
// invoked by, and must not be mistaken for, server-side enforcement;
// there is deliberately no such enforcement to call it from.
inline bool is_ascending(const EpIdMappingEntry* entries, size_t count) noexcept {
    if (count < 2) return true;
    for (size_t i = 1; i < count; ++i) {
        const EpIdMappingEntry& prev = entries[i - 1];
        const EpIdMappingEntry& cur  = entries[i];
        if (prev.request_stream_index != cur.request_stream_index) {
            if (!(prev.request_stream_index < cur.request_stream_index)) return false;
            continue;
        }
        if (!(prev.byte_bus_id < cur.byte_bus_id)) return false;
    }
    return true;
}

// effective_count — REQ-RMAP-054, ported from c-RCP's
// rcp_regmap_ep_id_map_effective_count(). Returns the number of leading
// rows in entries[0..capacity) that precede the first row whose
// request_stream_index == 0 (TC18's own defined end-of-table sentinel);
// if no row in that range is a sentinel, returns capacity unchanged.
// Read-only -- does not modify entries.
inline size_t effective_count(const EpIdMappingEntry* entries, size_t capacity) noexcept {
    for (size_t i = 0; i < capacity; ++i) {
        if (entries[i].request_stream_index == 0u) return i;
    }
    return capacity;
}

// row_init_default — REQ-RMAP-084, ported from c-RCP's
// rcp_regmap_ep_id_map_row_init_default(). TC18 §12.7.8 requires the
// table's power-on default contents to permit access to EP0 before any
// configuration is written: request_stream_index = 1 (the smallest value
// that is a valid stream index rather than the end-of-table sentinel),
// ep_id = kEp0, byte_bus_id = 0. Callers that own a fixed-capacity table
// are expected to place the result at row 0 at startup, before any
// client write.
inline void row_init_default(EpIdMappingEntry& row) noexcept {
    row.request_stream_index = 1u;
    row.ep_id                = kEp0;
    row.byte_bus_id          = 0u;
}

// render — REQ-RMAP-052/053, ported from c-RCP's
// rcp_regmap_ep_id_map_render(). Serializes entries[0..count) into out at
// each row's own 4-octet stride. ep_id is truncated to the wire's real
// 8-bit EP_Nr width. byte_bus_id/Ctrl at row-relative offset 0x0002 is
// packed per Table 25/26: byte_bus_id masked to its own real 11-bit wire
// width and shifted into bits[15:5], crc_required at bit 4,
// Channel_selection (bits[3:0]) always 0 -- deliberately unimplemented.
inline void render(const EpIdMappingEntry* entries, size_t count, uint8_t* out) noexcept {
    for (size_t i = 0; i < count; ++i) {
        uint8_t* row = out + kRowLen * i;
        row[0] = entries[i].request_stream_index;
        row[1] = static_cast<uint8_t>(entries[i].ep_id);

        uint16_t bbid_ctrl = static_cast<uint16_t>((entries[i].byte_bus_id & 0x07FFu) << 5);
        bbid_ctrl = static_cast<uint16_t>(bbid_ctrl | (entries[i].crc_required ? 0x10u : 0x00u));
        avtp::detail::put_u16(&row[2], bbid_ctrl);
    }
}

// apply_reconfig — REQ-RMAP-052/053/054, ported from c-RCP's
// rcp_regmap_ep_id_map_apply_reconfig(). Same render-patch-reparse idiom
// every pointed-to table in this file uses. Every octet of every row is
// R/W+ (no read-only sub-fields within a row), so no octet is ever
// silently skipped. count itself (the table's own current row count) is
// never changed by this call. Channel_selection (bits[3:0] of the
// byte_bus_id/Ctrl word) is read but intentionally discarded, not stored
// to any field.
inline std::error_code apply_reconfig(EpIdMappingEntry* entries, size_t count,
                                       uint16_t relative_start_address, const uint8_t* data,
                                       size_t data_len) noexcept {
    if (data_len == 0u) return make_error_code(EpIdMapReconfigErrc::short_write);
    if (count > kMaxEntries) return make_error_code(EpIdMapReconfigErrc::out_of_range);

    const size_t block_len = count * kRowLen;
    if (static_cast<size_t>(relative_start_address) + data_len > block_len)
        return make_error_code(EpIdMapReconfigErrc::out_of_range);

    std::array<uint8_t, kMaxEntries * kRowLen> block{};
    render(entries, count, block.data());
    for (size_t i = 0; i < data_len; ++i) block[relative_start_address + i] = data[i];

    for (size_t i = 0; i < count; ++i) {
        const uint16_t bbid_ctrl = avtp::detail::get_u16(&block[kRowLen * i + 2]);
        entries[i].request_stream_index = block[kRowLen * i + 0];
        entries[i].ep_id                = block[kRowLen * i + 1]; // zero-extends
        entries[i].byte_bus_id  = static_cast<avtp::ByteBusId>((bbid_ctrl >> 5) & 0x07FFu);
        entries[i].crc_required = (bbid_ctrl & 0x10u) != 0u;
    }

    return {};
}

// has_single_client_per_ep — REQ-RMAP-057, ported from c-RCP's
// rcp_regmap_ep_id_map_has_single_client_per_ep(). Read-only diagnostic:
// TC18 §12.7.8 recommends, for safety reasons, that an endpoint be mapped
// to at most one RC Client (one request_stream_index) at a time. Returns
// true iff no ep_id in entries[0..count) is associated with two
// different request_stream_index values. O(count^2); count is expected
// to stay small (one server's own endpoint set).
inline bool has_single_client_per_ep(const EpIdMappingEntry* entries, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (entries[i].ep_id == entries[j].ep_id &&
                entries[i].request_stream_index != entries[j].request_stream_index) {
                return false;
            }
        }
    }
    return true;
}

// shared_bus_homogeneous — REQ-RMAP-058, ported from c-RCP's
// rcp_regmap_ep_id_map_shared_bus_homogeneous(). Read-only diagnostic:
// TC18 §12.7.8 recommends that endpoints sharing a byte_bus_id within one
// request stream share the same ep_type. ep_types[] is a caller-supplied,
// index-parallel array (ep_types[i] is entries[i]'s own endpoint's
// ep_type, looked up elsewhere -- this row carries no ep_type of its
// own). Returns true iff, for every group of rows sharing one
// (request_stream_index, byte_bus_id) pair, every ep_types[] value in
// that group is identical. O(count^2).
inline bool shared_bus_homogeneous(const EpIdMappingEntry* entries, const uint8_t* ep_types,
                                    size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (entries[i].request_stream_index == entries[j].request_stream_index &&
                entries[i].byte_bus_id == entries[j].byte_bus_id && ep_types[i] != ep_types[j]) {
                return false;
            }
        }
    }
    return true;
}

// ep_type_has_fixed_ep_id — REQ-WAKEUP-020, ported from c-RCP's
// rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(). Read-only diagnostic:
// checks that every row whose own ep_types[i] == target_ep_type has
// ep_id == required_ep_id (e.g. TC18 §13.7.2.1 fixes the WakeUp
// endpoint's own EP_Nr to 1). Vacuously true if no such row exists.
// O(count). This function has no dependency on any concrete endpoint-type
// header -- target_ep_type/required_ep_id are caller-supplied.
inline bool ep_type_has_fixed_ep_id(const EpIdMappingEntry* entries, const uint8_t* ep_types,
                                     size_t count, uint8_t target_ep_type,
                                     uint16_t required_ep_id) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (ep_types[i] == target_ep_type && entries[i].ep_id != required_ep_id) return false;
    }
    return true;
}

// byte_bus_ids_for_stream — REQ-E2E-029/030/045 (issue #335), ported from
// c-RCP's rcp_regmap_ep_id_map_byte_bus_ids_for_stream(). The "cross-
// endpoint orchestrator query": given entries[0..count), writes every
// DISTINCT byte_bus_id whose own row names request_stream_index into
// out_byte_bus_ids[0..out_capacity), skipping a byte_bus_id already
// written. byte_bus_id, not ep_id, is this function's own return unit --
// a caller's own dispatch mechanism resolves a live endpoint by
// byte_bus_id, never by raw ep_id alone (TC18 §12.9.1: "the EPs are
// mapped by their byte_bus_ids"). Returns the TOTAL number of distinct
// byte_bus_id values found, which may exceed out_capacity -- the same
// "ask first, then size a buffer" idiom this codebase's own scheduler
// uses elsewhere. out_byte_bus_ids may be nullptr iff out_capacity == 0.
// O(count^2); count is expected to stay small.
inline size_t byte_bus_ids_for_stream(const EpIdMappingEntry* entries, size_t count,
                                       uint8_t request_stream_index,
                                       avtp::ByteBusId* out_byte_bus_ids,
                                       size_t out_capacity) noexcept {
    size_t found = 0;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].request_stream_index != request_stream_index) continue;

        bool already_written = false;
        for (size_t j = 0; j < i; ++j) {
            if (entries[j].request_stream_index == request_stream_index &&
                entries[j].byte_bus_id == entries[i].byte_bus_id) {
                already_written = true;
                break;
            }
        }
        if (already_written) continue;

        if (found < out_capacity) out_byte_bus_ids[found] = entries[i].byte_bus_id;
        ++found;
    }
    return found;
}

// is_valid_association — REQ-LIFECYCLE-025/031 (issue #341 lineage),
// ported from c-RCP's rcp_regmap_ep_id_map_is_valid_association(). The
// membership test TC18 §12.3.1.2's "the request needs to come either via
// the discovery stream or via a valid stream_id/byte_bus_id combination"
// case needs: given a caller-resolved (request_stream_index, byte_bus_id)
// pair (resolve the raw stream_id first via
// request_stream_cfg::resolve_index() above -- this function does not
// re-derive it), true iff some row in entries[0..count) names EXACTLY
// that pair, regardless of which ep_id that row's own owning endpoint is
// -- "regardless of which endpoint owns it" is the operative distinction
// from EpClient's own single-endpoint-scoped via_owning_stream: TC18's
// "any valid stream_id/byte_bus_id combination" text does not require the
// combination to belong to the endpoint whose field is being written,
// only that the combination itself is a real, currently-configured one.
// entries may be nullptr iff count == 0 (returns false). O(count), a
// single linear scan.
//
// THIS is what unblocks batch A's writer_ctx() (see below): before this
// function existed in this port, via_valid_stream_association had no way
// to evaluate TC18 §12.3.1.2's rule and was pinned fail-closed (always
// false).
inline bool is_valid_association(const EpIdMappingEntry* entries, size_t count,
                                  uint8_t request_stream_index, avtp::ByteBusId byte_bus_id) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].request_stream_index == request_stream_index &&
            entries[i].byte_bus_id == byte_bus_id) {
            return true;
        }
    }
    return false;
}

} // namespace ep_id_map

// ── Response / ack queue config (extraction §3.10; TC18 §12.7.9 Table 27) ───
// ResponseQueueConfig — REQ-RMAP-059..065, replaces this codebase's own
// pre-rewrite v2.x placeholder shape (response_queue_size/ack_queue_size/
// flush_time -- confirmed, before replacing them, that nothing outside
// this file/its own tests reads or writes any of those three fields; the
// only other references anywhere in this codebase, rcp/respqueue.hpp's
// and rcp/deadline.hpp's own comments, name fields --
// ResponseQueueConfig::queue_size/max_avtpdu_size -- that never actually
// existed on the old struct either, i.e. those comments were already
// stale/aspirational, not a real dependency to preserve) with c-RCP's own
// rcp_regmap_response_queue_cfg_t (include/rcp/regmap.h:2457-2529,
// src/regmap.c): one row per response/ack queue, matching TC18's own
// per-queue register layout exactly.
struct ResponseQueueConfig {
    uint16_t stream_uid      = 0; // REQ-RMAP-060 (Table 27, relative 0x0000, 16 bit, R/W+):
                                   // bits [63:48] (unique_id half) of the stream_id this
                                   // queue transmits on -- see response_queue_stream_id()
    uint16_t max_avtpdu_size = 0; // REQ-RMAP-061 (Table 27, relative 0x0002, 16 bit, R/W*):
                                   // max AVTPDU length, in quadlets, this queue generates
    uint16_t queue_size      = 0; // REQ-RMAP-059 (Table 27, relative 0x0004, 16 bit, R/W*):
                                   // this queue's configured transmit-memory reservation,
                                   // in 32-bit words
    uint16_t flush_on_count  = 0; // REQ-RMAP-063 (Table 27, relative 0x0006, 16 bit, R/W+):
                                   // queued-octet threshold that triggers a flush
    uint32_t flush_time_us   = 0; // REQ-RMAP-064/065 (Table 27, relative 0x0008, 16 bit,
                                   // R/W+, microseconds): elapsed-since-last-transmission
                                   // threshold that forces a flush even of an empty queue.
                                   // Deliberately wider than the 16-bit wire register --
                                   // rcp/respqueue.hpp's own should_flush_by_time() already
                                   // takes an even wider elapsed/threshold pair -- a genuine
                                   // content/wire width mismatch: render() below saturates
                                   // (never wraps) a value exceeding 0xFFFF.
};

// response_queue_stream_id — REQ-RMAP-060, ported from c-RCP's
// rcp_regmap_response_queue_stream_id(). Builds the full stream_id cfg's
// queue transmits on, given the interface's own mac -- cfg.stream_uid
// supplies the suffix half.
inline avtp::StreamId response_queue_stream_id(const ResponseQueueConfig& cfg,
                                                const std::array<uint8_t, 6>& mac) noexcept {
    avtp::StreamId id;
    id.mac    = mac;
    id.suffix = cfg.stream_uid;
    return id;
}

// ResponseQueueCfgReconfigErrc — REQ-RMAP-061/065, ported from c-RCP's
// rcp_regmap_response_queue_cfg_reconfig_errc_t.
enum class ResponseQueueCfgReconfigErrc : int {
    short_write  = 1,
    out_of_range = 2,
};

inline const std::error_category& response_queue_cfg_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.response_queue_cfg_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<ResponseQueueCfgReconfigErrc>(ev)) {
            case ResponseQueueCfgReconfigErrc::short_write:
                return "rcp/regmap: response-queue-config write has no data";
            case ResponseQueueCfgReconfigErrc::out_of_range:
                return "rcp/regmap: response-queue-config write extends past the table's own current extent";
            default:
                return "rcp/regmap: response-queue-config unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(ResponseQueueCfgReconfigErrc e) noexcept {
    return {static_cast<int>(e), response_queue_cfg_reconfig_category()};
}

namespace response_queue_cfg {

// kRowLen — REQ-RMAP-061: TC18's own 10-octet-per-queue wire stride
// (STREAM_UID@0x0000, Max_AVTPDUsize@0x0002, queue_size@0x0004,
// flush_on_count@0x0006, Flush_time@0x0008).
constexpr size_t kRowLen = 10;

// kMaxEntries is not itself TC18-derived -- matches every sibling table's
// own identical, not-spec-derived bound.
constexpr size_t kMaxEntries = 64;

// render — REQ-RMAP-061, ported from c-RCP's
// rcp_regmap_response_queue_cfg_render(). Serializes entries[0..count)
// into out at each row's own 10-octet stride. flush_time_us is saturated
// (never wrapped) to 0xFFFF if it exceeds the wire register's own 16-bit
// range.
inline void render(const ResponseQueueConfig* entries, size_t count, uint8_t* out) noexcept {
    for (size_t i = 0; i < count; ++i) {
        const uint16_t flush_time_wire = (entries[i].flush_time_us > 0xFFFFu)
                                              ? uint16_t{0xFFFFu}
                                              : static_cast<uint16_t>(entries[i].flush_time_us);
        uint8_t* row = out + kRowLen * i;
        avtp::detail::put_u16(&row[0], entries[i].stream_uid);
        avtp::detail::put_u16(&row[2], entries[i].max_avtpdu_size);
        avtp::detail::put_u16(&row[4], entries[i].queue_size);
        avtp::detail::put_u16(&row[6], entries[i].flush_on_count);
        avtp::detail::put_u16(&row[8], flush_time_wire);
    }
}

// apply_reconfig — REQ-RMAP-061/065, ported from c-RCP's
// rcp_regmap_response_queue_cfg_apply_reconfig(). Same render-patch-
// reparse idiom every pointed-to table in this file uses. A value parsed
// back for flush_time_us can never itself exceed 0xFFFF (the wire
// register's own full range), so no saturation is needed on this
// direction, unlike render()'s own.
inline std::error_code apply_reconfig(ResponseQueueConfig* entries, size_t count,
                                       uint16_t relative_start_address, const uint8_t* data,
                                       size_t data_len) noexcept {
    if (data_len == 0u) return make_error_code(ResponseQueueCfgReconfigErrc::short_write);
    if (count > kMaxEntries) return make_error_code(ResponseQueueCfgReconfigErrc::out_of_range);

    const size_t block_len = count * kRowLen;
    if (static_cast<size_t>(relative_start_address) + data_len > block_len)
        return make_error_code(ResponseQueueCfgReconfigErrc::out_of_range);

    std::array<uint8_t, kMaxEntries * kRowLen> block{};
    render(entries, count, block.data());
    for (size_t i = 0; i < data_len; ++i) block[relative_start_address + i] = data[i];

    for (size_t i = 0; i < count; ++i) {
        const uint8_t* row = &block[kRowLen * i];
        entries[i].stream_uid      = avtp::detail::get_u16(&row[0]);
        entries[i].max_avtpdu_size = avtp::detail::get_u16(&row[2]);
        entries[i].queue_size      = avtp::detail::get_u16(&row[4]);
        entries[i].flush_on_count  = avtp::detail::get_u16(&row[6]);
        entries[i].flush_time_us   = static_cast<uint32_t>(avtp::detail::get_u16(&row[8]));
    }

    return {};
}

} // namespace response_queue_cfg

// ── Optional-subsystem config sections: Network/PHY/time-synch/security
//    (REQ-RMAP-039, TC18 §12.7.11-.14) ────────────────────────────────────
// Ported from c-RCP's rcp_regmap_optional_subsystem_cfg_t (include/rcp/
// regmap.h:2965-3070, src/regmap.c). Unlike every other Table 20
// pointed-to table this file models (HW_config, EP_ID_config, response-
// queue-config, request-stream-cfg, ep_generic_cfg), TC18 defines NO
// field-level layout for any of these four sections -- each one's own
// section text states verbatim "The content is product specific"
// (§12.7.11: "In case of an Ethernet interface this section comprised the
// entire MAC configuration"; §12.7.12: "might be empty" for
// MDIO-managed PHYs; §12.7.13: "Typically, gPTP (IEEE802.1AS)"; §12.7.14:
// "Typically, MacSec... and specifics of the key agreement"). There is
// therefore no row-typed struct here the way HW_config/EP_ID_config/etc.
// have one: each section is a flat, capacity-bounded, opaque byte buffer
// -- a conformant implementation's whole job is making that buffer
// reachable at its own advertised [ptr, ptr+capacity) extent, not
// interpreting what is inside it. GeneralMap already carries the four
// pointer/capacity pairs these sections are addressed through
// (svr_network_interface_cfg_ptr/_capacity, svr_physical_layer_cfg_ptr/
// _capacity, svr_time_synch_cfg_ptr/_capacity, svr_security_cfg_ptr/
// _capacity — REQ-RMAP-039, batch A).
//
// The EP0 address-routed dispatcher that would ROUTE a live ACF_ABB
// read/write to one of these sections by absolute address (c-RCP's
// rcp_regmap_ep0_decode_write_request()/_decode_read_request()/
// _encode_read_response(), include/rcp/regmap.h:3072-3549) is
// DELIBERATELY NOT ported in this batch: it is a single ~500-line
// cross-cutting orchestrator tying together every sub-table this whole
// file models PLUS lifecycle-state/writer authorization (rcp_lifecycle_
// field_writable()/_w_plus(), including three separate table-specific
// authorization carve-outs) PLUS SEQUENCER_config's own ownership-aware
// per-octet access control (REQ-SEQ-013) -- none of which ROADMAP.md
// Phase 4 batch B's own scope list (this file's own header comment,
// items 1-6) names. This section's own storage type and its
// apply_reconfig() wire codec ARE ported (REQ-RMAP-039 explicitly is in
// scope) since a caller can already exercise them directly; only the
// unified address-routing dispatcher is deferred, exactly the same
// "content/mechanism ported, dispatcher not" split batch A already
// established for the sequencer/HW-pin/request-stream/response-stream/
// functional-config table pointers themselves.
namespace optional_subsystem_cfg {

// kMaxOctets — this implementation's own storage bound per
// optional-subsystem section, NOT a TC18-mandated limit (TC18 leaves
// each section's own real capacity entirely up to the product, via its
// own svr_*_cfg_capacity register) — matches every sibling table's own
// MAX_ENTRIES-style bound.
constexpr size_t kMaxOctets = 256;

} // namespace optional_subsystem_cfg

// OptionalSubsystemCfg — one flat byte buffer + its own currently-
// configured length, for one of the four optional-subsystem sections
// above. len mirrors the corresponding svr_*_cfg_capacity register in
// GeneralMap (kept in sync by whichever call installs this buffer — this
// port's own future caller's job, mirroring REQ-RMAP-032/034/036/037's
// already-established capacity-sync convention for other tables). A
// default-constructed instance (len == 0) correctly means "nothing
// installed", matching this table's own zero pointer/capacity register
// default (GeneralMap's own default member initializers) — TC18's own
// defined "not supported" encoding for three of the four (physical-layer/
// time-synch/security; network-interface has no such explicit note, see
// svr_network_interface_cfg_ptr's own field comment above).
struct OptionalSubsystemCfg {
    std::array<uint8_t, optional_subsystem_cfg::kMaxOctets> data{};
    size_t                                                  len = 0;
};

// OptionalSubsystemCfgReconfigErrc — REQ-RMAP-039, ported from c-RCP's
// rcp_regmap_optional_subsystem_cfg_reconfig_errc_t.
enum class OptionalSubsystemCfgReconfigErrc : int {
    short_write  = 1,
    out_of_range = 2,
};

inline const std::error_category& optional_subsystem_cfg_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.regmap.optional_subsystem_cfg_reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<OptionalSubsystemCfgReconfigErrc>(ev)) {
            case OptionalSubsystemCfgReconfigErrc::short_write:
                return "rcp/regmap: optional-subsystem config write has no data";
            case OptionalSubsystemCfgReconfigErrc::out_of_range:
                return "rcp/regmap: optional-subsystem config write extends past the section's own current extent";
            default:
                return "rcp/regmap: optional-subsystem config unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(OptionalSubsystemCfgReconfigErrc e) noexcept {
    return {static_cast<int>(e), optional_subsystem_cfg_reconfig_category()};
}

namespace optional_subsystem_cfg {

// apply_reconfig — REQ-RMAP-039, ported from c-RCP's
// rcp_regmap_optional_subsystem_cfg_apply_reconfig(). Applies a write of
// data[0..data_len) at relative_start_address within cfg's own current
// extent ([0, cfg.len)) — a direct bounded copy, not the render-patch-
// reparse idiom every row-typed table's own apply_reconfig() needs, since
// an opaque byte buffer has no rows to reparse. Fails ::out_of_range if
// the write would extend past cfg.len (this section's own currently-
// configured capacity, not kMaxOctets), ::short_write if data_len == 0 --
// matching every sibling apply_reconfig()'s own two failure modes exactly.
inline std::error_code apply_reconfig(OptionalSubsystemCfg& cfg, uint16_t relative_start_address,
                                       const uint8_t* data, size_t data_len) noexcept {
    if (data_len == 0u) return make_error_code(OptionalSubsystemCfgReconfigErrc::short_write);
    if (static_cast<size_t>(relative_start_address) + data_len > cfg.len)
        return make_error_code(OptionalSubsystemCfgReconfigErrc::out_of_range);

    std::copy(data, data + data_len, cfg.data.begin() + relative_start_address);
    return {};
}

} // namespace optional_subsystem_cfg

// ── svr_implemented_options bitmask ───────────────────────────────────────────
//
// Legacy, pre-Phase-4 bitmask constants (uint32_t) — retained ONLY because
// rcp/request.hpp's implemented_options_bits()/timed_feature_enabled() and
// rcp/e2e.hpp's implemented_options_bit() still reference
// kOptConditionalRequests/kOptSafetyRequests, and neither header is in this
// batch's scope. c-RCP's own REQ-RMAP-004..008 investigation (regmap.c —
// "primary-source verification ... found this citation incorrect: §12.9.1.1
// ... says nothing about svr_implemented_options, feature advertisement, or
// any bit-pairing rule") found that the all-or-nothing-pair grouping this
// design implies has no TC18 basis at all — "safety requests" and
// "fragmentation" bits in particular do not exist anywhere in the real
// Table 20 register (see the five REAL bits below, REQ-RMAP-030). Do not
// add new callers of these three legacy constants; new code should read/
// write GeneralMap::svr_implemented_options directly against
// kOptCompoundWait/kOptTrigger/kOptChained/kOptTimeSync/kOptEnhCancel.
constexpr uint32_t kOptConditionalRequests = 0x0000'0001;
constexpr uint32_t kOptSafetyRequests      = 0x0000'0002;
constexpr uint32_t kOptFragmentation       = 0x0000'0004;

// REQ-RMAP-030 (TC18 §12.7.5 Table 20, absolute address 0x0016, 8 bit, R):
// five independent bits, one per optional feature, "abcdefgh" with bits
// f/g/h reserved — verified directly against the primary-source PDF (c-RCP
// regmap.h/regmap.c, ported unchanged):
//   a: compound & wait requests
//   b: trigger requests
//   c: chained requests
//   d: time synch and timed requests
//   e: enhanced request cancellation
// These are GeneralMap::svr_implemented_options's real bits (uint8_t,
// matching the register's own 8-bit width) — distinct from, and not
// interchangeable with, the legacy uint32_t constants above.
constexpr uint8_t kOptCompoundWait = 1u << 0; // a
constexpr uint8_t kOptTrigger      = 1u << 1; // b
constexpr uint8_t kOptChained      = 1u << 2; // c
constexpr uint8_t kOptTimeSync     = 1u << 3; // d
constexpr uint8_t kOptEnhCancel    = 1u << 4; // e

// ── Sub-table pointer/capacity pairs ──────────────────────────────────────────
// The pointer/capacity pair pattern c-RCP's own regmap.h calls
// rcp_regmap_table_ref_t. `offset` is this implementation's own choice of
// addressing unit; `capacity` is the number of entries the table can hold,
// not necessarily how many are populated. GeneralMap's own Table 20 fields
// (below) do NOT use this shared type for most sub-tables: c-RCP's own
// REQ-RMAP-033/034/036/038 investigation found TC18 defines each pointer/
// capacity pair as two SEPARATE, independently-addressed, non-adjacent
// registers of their own specific widths (not a bundled offset+uint16_t
// capacity) — GeneralMap models each such pair as its own pair of scalar
// fields instead, matching TC18 exactly. TablePointer remains the right
// shape for the batch-B sub-tables below that still use it (this
// codebase's own pre-existing pointer/capacity fields for HW_config/
// request-stream/response-stream/EP-ID-map/functional-config).
struct TablePointer {
    uint32_t offset   = 0;
    uint16_t capacity = 0;
};

// ── The general register map (GeneralMap) ─────────────────────────────────────
// REQ-RMAP-003/023/026..039, ported from c-RCP's rcp_regmap_general_t
// (include/rcp/regmap.h:416-984, src/regmap.c). The one register block
// every RC Server exposes regardless of which endpoint types it implements.
//
// Zero-initialization plus svr_root_client_index defaulting to
// kNoRootClient is this port's equivalent of c-RCP's
// rcp_regmap_general_init() — no separate init() function is needed here
// since default member initializers already give every instance that same
// starting state (svr_lifecycle_state == 0 == HW_UNCONFIGURED, the correct
// default, matches every real server's own starting lifecycle::ServerState).

constexpr uint32_t kRegisterMapMagic = 0x52435030; // "RCP0" -- this implementation's own placeholder value

// kNoRootClient — sentinel for GeneralMap::svr_root_client_index meaning "no
// stream currently holds the root-client grant" — the natural state while
// HW_UNCONFIGURED, before any client has been promoted. Matches c-RCP's
// RCP_REGMAP_NO_ROOT_CLIENT.
constexpr uint16_t kNoRootClient = 0xFFFFu;

struct GeneralMap {
    uint32_t magic       = kRegisterMapMagic; // vendor/device-defined; this module carries the field only
    uint32_t svr_version = 0;                 // 32 bit wide on the wire, not 16 (c-RCP regmap.h)
    uint16_t vendor_id   = 0;
    uint16_t device_id   = 0;
    uint16_t svr_ep_count = 0;

    // REQ-RMAP-023 (TC18 §12.3.1.1/§12.3.1.2): the server's own
    // lifecycle::ServerState, cast to its wire representation. Content
    // modeling only: making this field actually readable over the wire is
    // its own still-open general register-read-dispatch gap (this field is
    // deliberately excluded from render() below — see render()'s own doc
    // comment) — a caller such as a future mock server keeps this in sync
    // after every successful lifecycle::ServerLifecycle::advance().
    uint8_t svr_lifecycle_state = 0;

    uint8_t  svr_req_stream_max = 0;          // REQ-RMAP-026 (Table 20, 0x000E, 8 bit, R): max request streams usable to access this server
    uint8_t  svr_responder_streams_max = 0;   // REQ-RMAP-026 (Table 20, 0x000F, 8 bit, R): max supported responder queues
    uint16_t svr_responder_mem_size = 0;      // REQ-RMAP-027 (Table 20, 0x0010, 16 bit, R): max responder-queue memory, in 32-bit words
    uint16_t svr_req_mem_size = 0;            // REQ-RMAP-027 (Table 20, 0x0012, 16 bit, R): max memory for EP request queues, in 32-bit words
    uint8_t  svr_sequencers_max = 0;          // REQ-RMAP-028 (Table 20, 0x0014, 8 bit, R): 0 = sequencer operation not supported; 1..n = available sequencer state registers
    uint8_t  svr_configuration_lock = 0;      // REQ-RMAP-029 (Table 20, 0x0015, 8 bit, R): 0x00 permits write access to R/W+ parameters; any other value rejects it

    // REQ-RMAP-030: RCP_REGMAP_OPT_*-equivalent bitmask (kOptCompoundWait/
    // kOptTrigger/kOptChained/kOptTimeSync/kOptEnhCancel above), 8 bit on
    // the wire.
    uint8_t svr_implemented_options = 0;

    uint8_t reserved_0x17 = 0; // REQ-RMAP-031 (Table 20, 0x0017, 8 bit): reserved, must read 0x00
    uint16_t svr_io_pin_count = 0; // REQ-RMAP-032 (Table 20, 0x0018, 16 bit, R): number of assignable I/O pins

    // svr_root_client_index — passive Table-20 storage (kNoRootClient if
    // unset). Not rendered on the wire by render() below (see its own doc
    // comment) — a caller composing a real RC Server keeps this field and
    // Ep0::claim_root_client()'s own session-scoped root-client tracking in
    // sync itself; this header does not wire the two together automatically
    // (Ep0's own `client` identifiers are opaque, transport-assigned
    // indices, not necessarily this field's own uint16_t stream-index
    // width).
    uint16_t svr_root_client_index = kNoRootClient;

    uint16_t svr_hw_cfg_ptr = 0; // REQ-RMAP-033 (Table 20, 0x001A, 16 bit, R): address of the HW_config register map (batch B)

    uint8_t  svr_request_stream_cfg_capacity  = 0; // REQ-RMAP-034 (Table 20, 0x001C, 8 bit, R)
    uint8_t  svr_response_stream_cfg_capacity = 0; // REQ-RMAP-034 (Table 20, 0x001D, 8 bit, R)
    uint16_t svr_request_stream_cfg_ptr       = 0; // REQ-RMAP-034 (Table 20, 0x001E, 16 bit, R)
    uint16_t svr_response_stream_cfg_ptr      = 0; // REQ-RMAP-034 (Table 20, 0x0020, 16 bit, R)

    uint16_t reserved_0x22 = 0; // REQ-RMAP-035 (Table 20, 0x0022, 16 bit): reserved, must read 0x00

    uint16_t svr_ep_generic_cfg_ptr      = 0; // REQ-RMAP-036 (Table 20, 0x0024, 16 bit, R): address of the EP_config register map (§13.2)
    uint16_t svr_ep_generic_cfg_capacity = 0; // REQ-RMAP-036 (Table 20, 0x0026, 16 bit, R): LENGTH OF THE EP CONFIG REGISTER SECTION IN BYTES -- a byte length, not an entry count

    uint16_t svr_ep_bytebus_id_map_ptr      = 0; // REQ-RMAP-037 (Table 20, 0x0028, 16 bit, R): address of the EP - byte_bus_id mapping table (batch B, §12.7.8)
    uint8_t  svr_ep_bytebus_id_map_capacity = 0; // REQ-RMAP-037 (Table 20, 0x002A, 8 bit, R): max entries in that table

    // 0x002B: inferred, unconfirmed one-octet alignment gap between
    // svr_ep_bytebus_id_map_capacity and svr_ep_functional_cfg_ptr -- TC18's
    // own table has no explicit "reserved" row there. Not modeled as a
    // field (render() below writes 0x00 there directly); see render()'s own
    // doc comment.

    uint16_t svr_ep_functional_cfg_ptr = 0; // REQ-RMAP-038 (Table 20, 0x002C, 16 bit, R): address of the EP_FUNC_config register map (§13.7.1.2)
    uint16_t svr_sequencer_state_ptr   = 0; // REQ-RMAP-038 (Table 20, 0x002E, 16 bit, R): address of the Sequencer_config register map (§12.7.10) -- content storage (RegisterMap::sequencer_states) exists; the wire codec for this specific table is not ported (REQ-SEQ-014, out of batch B's own scope -- see this file's own "Phase 4 batch B" banner)

    // REQ-RMAP-039 (Table 20 continued -- every address in this group is
    // INFERRED, not directly read: TC18's own "Absolute address" column is
    // blank for this whole continuation page. See c-RCP regmap.h's own
    // svr_network_interface_cfg_ptr comment for the full derivation.
    uint16_t svr_network_interface_cfg_ptr      = 0; // 0x0030 (inferred): address of the Network_config register map; 0 = not supported
    uint16_t svr_network_interface_cfg_capacity = 0; // 0x0032 (inferred)
    uint16_t svr_physical_layer_cfg_ptr         = 0; // 0x0034 (inferred): address of the physical-layer configuration register map; 0 = not supported
    uint16_t svr_physical_layer_cfg_capacity    = 0; // 0x0036 (inferred)
    uint16_t svr_time_synch_cfg_ptr             = 0; // 0x0038 (inferred): address of the PTP_config register map; 0 = not supported
    uint16_t svr_time_synch_cfg_capacity        = 0; // 0x003A (inferred)
    uint16_t svr_security_cfg_ptr               = 0; // 0x003C (inferred): address of the security configuration register map; 0 = not supported
    uint16_t svr_security_cfg_capacity          = 0; // 0x003E (inferred)

    // REQ-RMAP-039 (issue #429): Table 20's own true final pair, immediately
    // following svr_security_cfg_capacity. TC18's own printed description
    // text for this specific pair is genuinely swapped between the two rows
    // (a known, still-open drafting issue in the primary source itself,
    // "051RC5 - proposal to solve TI_032") -- named _ptr first, _capacity
    // second here to match every other Group 1 pair's own address-order
    // convention regardless.
    uint16_t svr_device_specific_cfg_ptr      = 0; // 0x0040 (inferred)
    uint16_t svr_device_specific_cfg_capacity = 0; // 0x0042 (inferred)
};

// ── Table 20 wire codec (REQ-RMAP-024) ────────────────────────────────────────
// Every field above is documented against its own TC18 §12.7.5 Table 20
// absolute address. The functions below are the same wire mechanism
// discovery.hpp already uses for its own narrower identity slice (a plain
// ACF_ABB read addressed to byte_bus_id 0 / EP0), generalized to serve
// GeneralMap's full Table 20 extent — ported from c-RCP's
// rcp_regmap_general_render()/_encode_read_response()/_decode_read_response()/
// _decode_write_request() (src/regmap.c).

// kGeneralMapLen — total wire length (bytes) of GeneralMap's TC18 §12.7.5
// Table 20 extent, absolute address 0x0000 through 0x0043 inclusive.
constexpr size_t kGeneralMapLen = 0x0044u;
using GeneralMapImage = std::array<uint8_t, kGeneralMapLen>;

// render — REQ-RMAP-024/039. Serializes map's Table 20 fields into the
// returned image at each field's own TC18-documented absolute address.
//
// Deliberately excludes svr_lifecycle_state and svr_root_client_index:
// neither has a genuine Table 20 address -- both are this struct's own
// convenience placement of what TC18 §13.7.1.2 Table 33/36 (the RC
// Server's own EP_func block, reached via svr_ep_functional_cfg_ptr, a
// *different*, pointer-addressed mechanism) actually owns. Rendering
// either field into this Table 20 image at an invented address would be a
// real conformance defect, not a harmless placeholder (c-RCP regmap.h's
// own REQ-RMAP-023 doc comment).
//
// The one-byte gap at absolute address 0x002B (between
// svr_ep_bytebus_id_map_capacity's own 0x002A and svr_ep_functional_cfg_
// ptr's own 0x002C) is written as 0x00: TC18's own table has no explicit
// "reserved" row there the way 0x0017 and 0x0022 both do -- an
// unconfirmed, inferred single-octet alignment gap, not a directly-cited
// reserved register.
inline GeneralMapImage render(const GeneralMap& map) noexcept {
    GeneralMapImage out{};

    avtp::detail::put_u32(&out[0x0000], map.magic);
    avtp::detail::put_u32(&out[0x0004], map.svr_version);
    avtp::detail::put_u16(&out[0x0008], map.vendor_id);
    avtp::detail::put_u16(&out[0x000A], map.device_id);
    avtp::detail::put_u16(&out[0x000C], map.svr_ep_count);
    // 0x000E..0x000F: svr_lifecycle_state deliberately NOT rendered -- see
    // this function's own doc comment above.
    out[0x000E] = map.svr_req_stream_max;
    out[0x000F] = map.svr_responder_streams_max;
    avtp::detail::put_u16(&out[0x0010], map.svr_responder_mem_size);
    avtp::detail::put_u16(&out[0x0012], map.svr_req_mem_size);
    out[0x0014] = map.svr_sequencers_max;
    out[0x0015] = map.svr_configuration_lock;
    out[0x0016] = map.svr_implemented_options;
    out[0x0017] = map.reserved_0x17;
    avtp::detail::put_u16(&out[0x0018], map.svr_io_pin_count);
    // svr_root_client_index deliberately NOT rendered -- same exclusion as
    // svr_lifecycle_state above.
    avtp::detail::put_u16(&out[0x001A], map.svr_hw_cfg_ptr);
    out[0x001C] = map.svr_request_stream_cfg_capacity;
    out[0x001D] = map.svr_response_stream_cfg_capacity;
    avtp::detail::put_u16(&out[0x001E], map.svr_request_stream_cfg_ptr);
    avtp::detail::put_u16(&out[0x0020], map.svr_response_stream_cfg_ptr);
    avtp::detail::put_u16(&out[0x0022], map.reserved_0x22);
    avtp::detail::put_u16(&out[0x0024], map.svr_ep_generic_cfg_ptr);
    avtp::detail::put_u16(&out[0x0026], map.svr_ep_generic_cfg_capacity);
    avtp::detail::put_u16(&out[0x0028], map.svr_ep_bytebus_id_map_ptr);
    out[0x002A] = map.svr_ep_bytebus_id_map_capacity;
    // 0x002B: inferred, unconfirmed one-octet alignment gap -- left 0x00 by
    // GeneralMapImage's own zero-initialization above.
    avtp::detail::put_u16(&out[0x002C], map.svr_ep_functional_cfg_ptr);
    avtp::detail::put_u16(&out[0x002E], map.svr_sequencer_state_ptr);
    avtp::detail::put_u16(&out[0x0030], map.svr_network_interface_cfg_ptr);
    avtp::detail::put_u16(&out[0x0032], map.svr_network_interface_cfg_capacity);
    avtp::detail::put_u16(&out[0x0034], map.svr_physical_layer_cfg_ptr);
    avtp::detail::put_u16(&out[0x0036], map.svr_physical_layer_cfg_capacity);
    avtp::detail::put_u16(&out[0x0038], map.svr_time_synch_cfg_ptr);
    avtp::detail::put_u16(&out[0x003A], map.svr_time_synch_cfg_capacity);
    avtp::detail::put_u16(&out[0x003C], map.svr_security_cfg_ptr);
    avtp::detail::put_u16(&out[0x003E], map.svr_security_cfg_capacity);
    avtp::detail::put_u16(&out[0x0040], map.svr_device_specific_cfg_ptr);
    avtp::detail::put_u16(&out[0x0042], map.svr_device_specific_cfg_capacity);

    return out;
}

// encode_read_response — REQ-RMAP-024. Encodes an ACF_ABB read RESPONSE
// addressed to byte_bus_id 0 (EP0), carrying min(read_size, kGeneralMapLen)
// octets of map's own render() image starting at absolute address 0, with
// any remaining requested octets (up to read_size) zero-filled -- the same
// "response spans exactly read_size octets" convention discovery.hpp
// already establishes for its own narrower slice.
inline std::vector<uint8_t> encode_read_response(const GeneralMap& map, uint8_t read_size,
                                                   uint8_t transaction_num) {
    const GeneralMapImage image = render(map);
    std::vector<uint8_t>  payload(read_size, 0);
    const size_t          copy_len = std::min<size_t>(read_size, image.size());
    std::copy(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(copy_len), payload.begin());

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id              = static_cast<avtp::ByteBusId>(kEp0);
    hdr.op                       = false; // read
    hdr.rsp                      = true;
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    return acf::encode_acf_abb(hdr, payload);
}

// decode_read_response — REQ-RMAP-024/039. Decodes and validates an
// ACF_ABB general-register-map read RESPONSE from b[0..len). On success,
// out_map has every Table 20 field render() populates overwritten from
// whichever prefix of kGeneralMapLen the response payload actually carries
// -- a short response (fewer octets than the full extent) leaves the
// remaining, un-carried fields of out_map untouched past the first
// unconditional 14-octet (magic/svr_version/vendor_id/device_id/
// svr_ep_count) group, so a caller must default-construct (or otherwise
// define) out_map before calling this. svr_lifecycle_state and
// svr_root_client_index are never touched, for the same reason render()
// never renders them.
//
// Faithfully reproduces c-RCP's own single-checkpoint short-frame handling
// (rcp_regmap_general_decode_read_response(), src/regmap.c): there is
// exactly one early-return length check (at absolute address 0x000E,
// immediately after the first 5 fields), not a per-field one -- a response
// whose payload covers anything past 0x000E populates every remaining
// field, reading zero for any octet range beyond what the payload actually
// carried (out_map's own zero-padded local image, not out_map's own prior
// value). This matches c-RCP's real, currently-shipping behavior, not just
// its doc comment's own narrower framing of it -- ported unchanged rather
// than silently "fixed" against the reference implementation.
inline std::error_code decode_read_response(const uint8_t* b, size_t len, GeneralMap& out_map) noexcept {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    const std::error_code acf_ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (acf_ec) {
        if (acf_ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer))
            return make_error_code(GeneralMapErrc::short_frame);
        return make_error_code(GeneralMapErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != static_cast<avtp::ByteBusId>(kEp0)) return make_error_code(GeneralMapErrc::wrong_bus);
    if (hdr.op) return make_error_code(GeneralMapErrc::wrong_op); // op=true is write; a read response is expected

    GeneralMapImage image{};
    const size_t    have = std::min(payload.size(), image.size());
    std::copy(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(have), image.begin());

    out_map.magic        = avtp::detail::get_u32(&image[0x0000]);
    out_map.svr_version  = avtp::detail::get_u32(&image[0x0004]);
    out_map.vendor_id    = avtp::detail::get_u16(&image[0x0008]);
    out_map.device_id    = avtp::detail::get_u16(&image[0x000A]);
    out_map.svr_ep_count = avtp::detail::get_u16(&image[0x000C]);
    if (have <= 0x000E) return {};

    out_map.svr_req_stream_max                  = image[0x000E];
    out_map.svr_responder_streams_max            = image[0x000F];
    out_map.svr_responder_mem_size               = avtp::detail::get_u16(&image[0x0010]);
    out_map.svr_req_mem_size                     = avtp::detail::get_u16(&image[0x0012]);
    out_map.svr_sequencers_max                   = image[0x0014];
    out_map.svr_configuration_lock               = image[0x0015];
    out_map.svr_implemented_options              = image[0x0016];
    out_map.reserved_0x17                        = image[0x0017];
    out_map.svr_io_pin_count                     = avtp::detail::get_u16(&image[0x0018]);
    out_map.svr_hw_cfg_ptr                       = avtp::detail::get_u16(&image[0x001A]);
    out_map.svr_request_stream_cfg_capacity      = image[0x001C];
    out_map.svr_response_stream_cfg_capacity     = image[0x001D];
    out_map.svr_request_stream_cfg_ptr           = avtp::detail::get_u16(&image[0x001E]);
    out_map.svr_response_stream_cfg_ptr          = avtp::detail::get_u16(&image[0x0020]);
    out_map.reserved_0x22                        = avtp::detail::get_u16(&image[0x0022]);
    out_map.svr_ep_generic_cfg_ptr               = avtp::detail::get_u16(&image[0x0024]);
    out_map.svr_ep_generic_cfg_capacity          = avtp::detail::get_u16(&image[0x0026]);
    out_map.svr_ep_bytebus_id_map_ptr            = avtp::detail::get_u16(&image[0x0028]);
    out_map.svr_ep_bytebus_id_map_capacity       = image[0x002A];
    out_map.svr_ep_functional_cfg_ptr            = avtp::detail::get_u16(&image[0x002C]);
    out_map.svr_sequencer_state_ptr              = avtp::detail::get_u16(&image[0x002E]);
    out_map.svr_network_interface_cfg_ptr        = avtp::detail::get_u16(&image[0x0030]);
    out_map.svr_network_interface_cfg_capacity   = avtp::detail::get_u16(&image[0x0032]);
    out_map.svr_physical_layer_cfg_ptr           = avtp::detail::get_u16(&image[0x0034]);
    out_map.svr_physical_layer_cfg_capacity      = avtp::detail::get_u16(&image[0x0036]);
    out_map.svr_time_synch_cfg_ptr               = avtp::detail::get_u16(&image[0x0038]);
    out_map.svr_time_synch_cfg_capacity          = avtp::detail::get_u16(&image[0x003A]);
    out_map.svr_security_cfg_ptr                 = avtp::detail::get_u16(&image[0x003C]);
    out_map.svr_security_cfg_capacity            = avtp::detail::get_u16(&image[0x003E]);
    out_map.svr_device_specific_cfg_ptr          = avtp::detail::get_u16(&image[0x0040]);
    out_map.svr_device_specific_cfg_capacity     = avtp::detail::get_u16(&image[0x0042]);
    return {};
}

// decode_write_request — REQ-RMAP-025. Decodes an ACF_ABB WRITE request
// from b[0..len) addressed to byte_bus_id 0 (EP0) targeting the Table 20
// general register map. Every one of Table 20's registers is TC18-defined
// access type R (read-only), so this never applies a write -- it exists
// only to recognize a genuine write attempt and report the correct wire
// error for it: out_error is always acf::WireErrorCode::LockedMemAccess
// (Table 20 has no writable field at all, so this outcome does not depend
// on lifecycle state) and out_transaction_num is populated; the caller
// builds the actual error response via acf::make_response()/
// acf::encode_error_payload(out_error).
inline std::error_code decode_write_request(const uint8_t* b, size_t len, acf::WireErrorCode& out_error,
                                             uint8_t& out_transaction_num) noexcept {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    const std::error_code acf_ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (acf_ec) {
        if (acf_ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer))
            return make_error_code(GeneralMapErrc::short_frame);
        return make_error_code(GeneralMapErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != static_cast<avtp::ByteBusId>(kEp0)) return make_error_code(GeneralMapErrc::wrong_bus);
    if (!hdr.op) return make_error_code(GeneralMapErrc::wrong_op); // op=false is read; a write request is expected

    out_error           = acf::WireErrorCode::LockedMemAccess;
    out_transaction_num = hdr.transaction_num;
    return {};
}

// ── Root-client / per-EP-restricted-client model ──────────────────────────────
// REQ-RMAP-009/010/011/012/070/086, ported from c-RCP's
// rcp_regmap_ep_client_t and rcp_regmap_writer_ctx() (include/rcp/
// regmap.h:1152-1211, src/regmap.c).

// EpClient — one endpoint's write-restriction: the single stream (if any)
// authorized to write that endpoint's functional config directly through
// its own registered request stream, per lifecycle::WriterCtx::
// via_owning_stream. has_owning_stream distinguishes "no owning stream
// configured" from stream index 0, which is itself a valid index.
struct EpClient {
    bool     has_owning_stream   = false;
    uint16_t owning_stream_index = 0;
};

// writer_ctx — derives a lifecycle::WriterCtx from this register map's
// root-client/owning-stream data for a request arriving on
// requesting_stream_index. via_ep0 must be true iff the request actually
// arrived through EP0; via_unicast must be true iff the request's frame had
// a unicast destination MAC; via_discovery_stream must be true iff the
// request arrived via the discovery stream -- this function does not
// re-derive any of the three from an address or stream role itself,
// matching acf.hpp/avtp.hpp's convention of taking already-classified
// inputs rather than re-parsing a frame. ep_client may be null, meaning
// "this endpoint has no owning stream on record" (via_owning_stream is
// then always false).
//
// REQ-LIFECYCLE-025/031 (issue #341 lineage, batch B): TC18 §12.3.1.2's
// own "any valid stream_id/byte_bus_id combination is accepted, but only
// when no root client is configured at all" rule is evaluated for real
// via ep_id_map::is_valid_association() (above) -- batch A's own
// EpIdMappingEntry did not yet carry request_stream_index, so this member
// was pinned fail-closed (always false) until that landed; it now
// evaluates a genuine, matching (request_stream_index, byte_bus_id) row.
// The "no root client configured at all" condition is baked directly into
// this member (not left to a caller to re-check separately), the same
// pattern via_root_client_ep0 above already establishes for its own
// svr_root_client_index check -- this member can therefore never wrongly
// widen access when a root client IS configured. requesting_stream_index
// is narrowed to uint8_t to match EpIdMappingEntry::request_stream_index's
// own field width (TC18 §12.7.8 Table 25: an 8-bit register).
inline lifecycle::WriterCtx writer_ctx(const GeneralMap& map, const EpClient* ep_client,
                                        uint16_t requesting_stream_index, bool via_ep0, bool via_unicast,
                                        bool via_discovery_stream,
                                        avtp::ByteBusId requesting_byte_bus_id,
                                        const EpIdMappingEntry* ep_id_map, size_t ep_id_map_count) noexcept {
    lifecycle::WriterCtx ctx;

    ctx.via_root_client_ep0 = via_ep0 && map.svr_root_client_index != kNoRootClient &&
                              requesting_stream_index == map.svr_root_client_index;

    ctx.via_owning_stream = ep_client != nullptr && ep_client->has_owning_stream &&
                            requesting_stream_index == ep_client->owning_stream_index;

    ctx.via_non_unicast_frame = !via_unicast;

    // REQ-RMAP-070: pass through, matching via_ep0/via_unicast's own
    // already-classified-input convention.
    ctx.via_discovery_stream = via_discovery_stream;

    ctx.via_valid_stream_association =
        map.svr_root_client_index == kNoRootClient &&
        ep_id_map::is_valid_association(ep_id_map, ep_id_map_count,
                                         static_cast<uint8_t>(requesting_stream_index),
                                         requesting_byte_bus_id);

    return ctx;
}

// ── RC Server's own functional-configuration content (TC18 §13.7.1.2) ────────
// REQ-RMAP-066/067, ported from c-RCP's rcp_regmap_svr_ep_cfg_t
// (include/rcp/regmap.h:1211-1303, src/regmap.c).
//
// TC18 §13.7.1.2 describes "Table 33/36: RC Server functional
// configuration" with two confirmed primary-source defects c-RCP's own
// investigation found and did NOT force-resolve: (1) the table's 8 register
// rows share only 5 distinct relative addresses (an address collision), and
// (2) §13.7.1.1's own prose states the RC Server "is not included in the
// EP_FUNC_config register maps" yet Table 33/36 lists it anyway. Given no
// evidence this codebase's own server ever dispatches the RC Server through
// the generic per-endpoint EP_FUNC mechanism, this struct models ONLY the
// two fields free of both defects: svr_discovery_timeout and svr_ep_status
// -- svr_root_client_index/svr_lifecycle_state are NOT duplicated here (see
// GeneralMap above for their own, separate content-modeling-only home).
//
// Zero-initialization plus svr_discovery_timeout defaulting to TC18's own
// stated power-on default is this port's equivalent of c-RCP's
// rcp_regmap_svr_ep_cfg_init().
struct SvrEpCfg {
    // svr_discovery_timeout -- REQ-RMAP-066: microseconds; TC18's own
    // stated default is 20000 (20 ms).
    uint16_t svr_discovery_timeout = 20000u;
    uint16_t svr_ep_status         = 0; // REQ-RMAP-067; TC18 gives no further bit-level breakdown at this citation
};

// ── RegisterMap ───────────────────────────────────────────────────────────────
// The general register-map fields needed to bootstrap everything else, plus
// the in-memory contents of the tables those pointer/capacity pairs
// describe. general/svr_ep_cfg are batch A's own GeneralMap/SvrEpCfg
// content; the pointer/capacity TablePointer fields below them remain this
// codebase's own pre-rewrite v2.x shape, unchanged by either batch (nothing
// in c-RCP's own Table 20 bundles pointer+capacity as one struct the way
// TablePointer does — see TablePointer's own doc comment above); the
// table-content vectors are this batch's own extended/replaced types
// (HwPinMapEntry, RequestStreamConfig, ResponseQueueConfig,
// EpIdMappingEntry — see each type's own doc comment above for what
// changed) plus the four new OptionalSubsystemCfg fields.

struct RegisterMap {
    GeneralMap general{};   // TC18 §12.7.5 Table 20 -- rcp_regmap_general_t
    SvrEpCfg   svr_ep_cfg{}; // TC18 §13.7.1.2 -- rcp_regmap_svr_ep_cfg_t

    // Pointer/capacity fields for the batch-B bootstrap tables.
    TablePointer hw_pin_map_table{};
    TablePointer request_stream_table{};
    TablePointer response_stream_table{};
    TablePointer ep_id_mapping_table{};
    TablePointer functional_config_table{};

    // Table contents.
    std::vector<HwPinMapEntry>            hw_pin_map;
    std::vector<RequestStreamConfig>      request_streams;
    std::vector<ResponseQueueConfig>      response_streams;
    std::vector<EpIdMappingEntry>         ep_id_mapping;
    std::vector<EndpointFunctionalConfig> functional_configs;

    // Per-endpoint generic config, indexed in parallel with functional_configs
    // (index i is EndpointId i+1 -- EP0 is not stored here, see Ep0 below).
    std::vector<EndpointGenericConfig> generic_configs;

    // Persistent sequencer-state storage (extraction §3.11, §3.16).
    std::vector<SequencerState> sequencer_states;

    // The four optional-subsystem sections (REQ-RMAP-039) — content
    // storage only; GeneralMap's own svr_network_interface_cfg_ptr/
    // _capacity (etc.) fields are this map's pointer/capacity registers
    // for these, kept in sync by whichever caller installs a buffer here.
    // A default-constructed (len == 0) instance means "not supported",
    // matching GeneralMap's own zero pointer/capacity default.
    OptionalSubsystemCfg network_interface_cfg{};
    OptionalSubsystemCfg physical_layer_cfg{};
    OptionalSubsystemCfg time_synch_cfg{};
    OptionalSubsystemCfg security_cfg{};
};

// ── EP0 -- RC Server as a pseudo-endpoint ─────────────────────────────────────
// extraction §5.1, §4.1: the RC Server itself is addressable like any other
// endpoint (as EP0) for whole-register-map reads and writes. Every client may
// read the whole map. Only one client — the root client — may write the
// whole map through EP0; every other client is restricted to writing the
// *functional* config of the endpoint(s) it owns. The generic config block is
// the RC Server's own and is writable by the root client alone, no matter
// who owns the endpoint (TC18 §13.1/§13.2, cpp-RCP-D2 — see
// check_write_access below).
//
// This class is this codebase's own original, richer session-tracking
// design layered on top of GeneralMap's own passive svr_root_client_index/
// EpClient data (see c-RCP's own file header: "rcp_regmap_writer_ctx()
// derives server.h's rcp_lifecycle_writer_ctx_t from this register data,
// without duplicating rcp_lifecycle_field_writable()'s already-built
// authorization logic" — this class is the analogous idea one layer up,
// scoped to EP0's own access-control decisions specifically). "Client" here
// is identified by an opaque index the embedding transport assigns per
// connected stream; this header does not know or care how that index maps
// to an avtp::StreamId, only that it is stable for the lifetime of the
// connection.
class Ep0 {
public:
    // ConfigBlock distinguishes which of an endpoint's two config blocks a
    // write targets, since the two lock at different lifecycle states (see
    // ServerLifecycle::generic_config_locked/functional_config_locked).
    enum class ConfigBlock { Generic, Functional };

    // endpoint_owner_ is the vector every access-control check (below)
    // bounds a target endpoint against, but the writes those checks gate
    // land in regs.generic_configs/regs.functional_configs instead — two
    // vectors this class does not own and cannot force the caller to keep
    // in sync with general.svr_ep_count. If they are not the same length as
    // general.svr_ep_count when this object is constructed, size
    // endpoint_owner_ to 0 rather than to the untrustworthy
    // general.svr_ep_count: every non-EP0 access check then fails closed
    // with invalid_parameter instead of handing out an in-range verdict
    // that a subsequent write turns into an out-of-bounds write on the
    // shorter/empty config vector (cpp-RCP-N2-01, issue #64).
    // write_generic_config/write_functional_config additionally
    // bounds-check against the actual vector being indexed as a second,
    // independent layer of defense (see below).
    Ep0(RegisterMap& regs, lifecycle::ServerLifecycle& lc) noexcept
        : regs_(regs), lifecycle_(lc),
          endpoint_owner_(size_invariant_holds(regs) ? regs.general.svr_ep_count : 0) {}

    // is_endpoint_table_consistent reports whether the size invariant this
    // class's access checks rely on currently holds (regs_.generic_configs
    // .size() == regs_.functional_configs.size() == regs_.general.svr_ep_count).
    // It is false only when the RegisterMap this Ep0 was constructed with
    // violated the invariant and per-endpoint access has therefore been
    // fail-closed (endpoint_owner_ sized to 0) until a valid replacement
    // map is installed via write_whole_map.
    bool is_endpoint_table_consistent() const noexcept {
        return size_invariant_holds(regs_) && endpoint_owner_.size() == regs_.general.svr_ep_count;
    }

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
    // client; a non-EP0 endpoint's write requires the root client, or — for
    // the *functional* config block only — that endpoint's assigned owner;
    // and a write to whichever config block is targeted is rejected while
    // the current lifecycle state has that specific block locked, regardless
    // of who is asking.
    //
    // The Generic/Functional asymmetry is the access-control rule TC18 §13.1
    // states (extraction: for RC Clients other than the ROOT_CLIENT, write
    // access is granted only to the *functional* config of endpoints
    // allocated to that client) and §13.2's own framing reinforces (the
    // generic part of the endpoint register map is owned by the RC Server,
    // not by the allocated client). Owning an endpoint therefore does NOT
    // confer write access to that endpoint's generic block: HW pin mapping,
    // request/response queue sizing, and the E2E-CRC enable toggles live
    // there, and a non-root client that could rewrite them would be able to
    // repoint another endpoint's hardware pins or silently disable a safety
    // mechanism (cpp-RCP-D2). Note that `block` defaults to
    // ConfigBlock::Generic, i.e. to the stricter of the two checks, so a
    // caller that forgets to name a block fails closed rather than open.
    std::error_code check_write_access(size_t client, EndpointId target,
                                        ConfigBlock block = ConfigBlock::Generic) const noexcept {
        if (target == kEp0) {
            return is_root_client(client) ? std::error_code{}
                                           : make_error_code(RegMapErrc::unauthorized_access);
        }
        if (target > endpoint_owner_.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        const auto& owner = endpoint_owner_[static_cast<size_t>(target - 1)];
        const bool owns = owner.has_value() && *owner == client;
        const bool authorized =
            is_root_client(client) || (block == ConfigBlock::Functional && owns);
        if (!authorized)
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

    // write_whole_map is the root-client-only half of the same rule. Beyond
    // the root-client check, it must also keep the size invariant every
    // access check in this class depends on intact (cpp-RCP-N2-02, issue
    // #65): a root client could otherwise install a replacement map whose
    // generic_configs/functional_configs disagree with its own
    // general.svr_ep_count, or that simply differs in general.svr_ep_count
    // from the map endpoint_owner_ was last sized against, either of which
    // lets a subsequent per-endpoint write pass a stale/incorrect bounds
    // check and land out of bounds on the new vectors. So: reject an
    // internally inconsistent replacement map outright, and resize
    // endpoint_owner_ to match the (now validated) new general.svr_ep_count
    // as part of the same operation, discarding prior per-endpoint owner
    // assignments — they describe endpoints in the map being replaced, not
    // necessarily the same endpoints in the new one.
    std::error_code write_whole_map(size_t client, RegisterMap new_map) noexcept {
        if (!is_root_client(client))
            return make_error_code(RegMapErrc::unauthorized_access);
        if (!size_invariant_holds(new_map))
            return make_error_code(RegMapErrc::invalid_parameter);
        regs_ = std::move(new_map);
        endpoint_owner_.assign(regs_.general.svr_ep_count, std::nullopt);
        return {};
    }

    // write_generic_config / write_functional_config apply the per-endpoint
    // write-access and per-block lock checks above before mutating the
    // targeted endpoint's config block in the underlying RegisterMap. The two
    // are deliberately not symmetric in who may call them: the generic block
    // is root-client-only, the functional block is root-or-owner (TC18
    // §13.1/§13.2 — see check_write_access above, cpp-RCP-D2). The
    // explicit size check against the vector actually being indexed (rather
    // than trusting check_write_access's endpoint_owner_-based verdict
    // alone) is a second, independent layer of defense against cpp-RCP-N2-01
    // (issue #64): even if endpoint_owner_ and the config vectors were ever
    // to drift apart again, these writes still cannot go out of bounds.
    std::error_code write_generic_config(size_t client, EndpointId target,
                                          EndpointGenericConfig cfg) noexcept {
        if (target == kEp0) return make_error_code(RegMapErrc::invalid_parameter);
        auto ec = check_write_access(client, target, ConfigBlock::Generic);
        if (ec) return ec;
        const auto idx = static_cast<size_t>(target - 1);
        if (idx >= regs_.generic_configs.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        regs_.generic_configs[idx] = std::move(cfg);
        return {};
    }

    std::error_code write_functional_config(size_t client, EndpointId target,
                                             EndpointFunctionalConfig cfg) noexcept {
        if (target == kEp0) return make_error_code(RegMapErrc::invalid_parameter);
        auto ec = check_write_access(client, target, ConfigBlock::Functional);
        if (ec) return ec;
        const auto idx = static_cast<size_t>(target - 1);
        if (idx >= regs_.functional_configs.size())
            return make_error_code(RegMapErrc::invalid_parameter);
        regs_.functional_configs[idx] = std::move(cfg);
        return {};
    }

private:
    // size_invariant_holds is the shared root-invariant check both the
    // constructor and write_whole_map enforce: every access-control check
    // above bounds a target endpoint against endpoint_owner_ (sized from
    // general.svr_ep_count), but the writes those checks gate land in
    // regs.generic_configs/regs.functional_configs — this must hold for
    // that to be safe (cpp-RCP-N2-01/N2-02, issues #64/#65).
    static bool size_invariant_holds(const RegisterMap& regs) noexcept {
        return regs.generic_configs.size() == regs.general.svr_ep_count &&
               regs.functional_configs.size() == regs.general.svr_ep_count;
    }

    RegisterMap&                       regs_;
    lifecycle::ServerLifecycle&        lifecycle_;
    std::optional<size_t>              root_client_;
    std::vector<std::optional<size_t>> endpoint_owner_;
};

} // namespace regmap
} // namespace rcp

// Enable std::error_code construction from every rcp::regmap error enum.
namespace std {
template <>
struct is_error_code_enum<rcp::regmap::RegMapErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::GeneralMapErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::EpGenericCfgReconfigErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::HwPinMapReconfigErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::RequestStreamCfgReconfigErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::EpIdMapReconfigErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::ResponseQueueCfgReconfigErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::OptionalSubsystemCfgReconfigErrc> : true_type {};
} // namespace std
