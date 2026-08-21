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
// Batch B (a separate, later PR) ports the rest: HW pin mapping, the
// named-signal index, request-stream-cfg, response-queue-cfg, the EP-ID/
// byte_bus_id map, and the optional-subsystem (Network/PHY/time-synch/
// security) config sections. Every type below already in this file's scope
// for THOSE sections — HwPinMapEntry, RequestStreamConfig,
// ResponseQueueConfig, EpIdMappingEntry, SequencerState, RxSafetyMeasure,
// EndpointFunctionalConfig (the opaque-blob placeholder), and RegisterMap's
// own hw_pin_map_table/request_stream_table/response_stream_table/
// ep_id_mapping_table/functional_config_table pointer/capacity pairs — is
// deliberately left exactly as this codebase's own pre-rewrite v2.x design
// already had it: those sections are batch B's job, not this pass's, and
// e2e.hpp/watchdog.hpp/deadline.hpp/sim.hpp/request.hpp/respqueue.hpp/
// gpio.hpp (and their own tests) already depend on their current shape.
//
// A genuine cross-cutting collision, found and resolved deliberately rather
// than silently: c-RCP's own rcp_regmap_ep_generic_cfg_t has NO per-role E2E
// CRC-enable fields at all (that content, in c-RCP, belongs to the
// *functional* config's ep_req_crc_enable, ONE field, not per-role) — but
// this codebase's own pre-rewrite EndpointGenericConfig already carries
// THREE (ep_req_crc_enable/ep_ack_crc_enable/ep_response_crc_enable), read
// by rcp/e2e.hpp's crc_required() and exercised by tests/test_e2e.cpp. Since
// rcp_regmap_ep_functional_cfg_t itself is NOT in this batch's scope (see
// the exclusion list above — it isn't named in ROADMAP.md Phase 4 batch A
// either) relocating those three fields to a functional-config struct that
// does not exist yet in this header would both be out of scope and break
// e2e.hpp today. They are kept on EndpointGenericConfig, unchanged, clearly
// marked below as this codebase's own pre-existing placement rather than
// c-RCP content — a documented deferral, not a silent inconsistency.
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

    // ── Pre-existing (pre-Phase-4), NOT c-RCP content ──────────────────────
    // rcp/e2e.hpp's crc_required() and tests/test_e2e.cpp already depend on
    // these three independently-settable per-role E2E CRC toggles
    // (ROADMAP.md milestone 50, v2.6.0) — kept here unchanged rather than
    // relocated, since c-RCP's own rcp_regmap_ep_functional_cfg_t (the
    // struct these conceptually belong to in c-RCP, as a single
    // ep_req_crc_enable field, not three per-role ones) is explicitly out
    // of this batch's scope. See this file's own header comment for the
    // full rationale.
    bool ep_req_crc_enable      = false;
    bool ep_ack_crc_enable      = false;
    bool ep_response_crc_enable = false;
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

// ── HW pin-mapping config (extraction §3.7) — batch B ────────────────────────
// Left exactly as this codebase's own pre-rewrite v2.x design already had
// it; c-RCP's own richer HW_config table (Table 21/22) is batch B's job.

struct HwPinMapEntry {
    uint16_t pin_id   = 0; // physical pin identifier; numbering is implementation-defined
    uint8_t  function = 0; // pin function/mode selector; meaning is endpoint-type-defined
};

// ── Sequencer-state registers (extraction §3.11, §3.16) — batch B ────────────
// Persistent 8-bit values; behavior lives in rcp/request.hpp (v2.5.0).

using SequencerState = uint8_t;

// ── rx_safety_measure selector (extraction §3.8) — batch B ───────────────────

enum class RxSafetyMeasure : uint8_t {
    ForceHighImpedance = 0, // hold outputs high-impedance; no sequencer consulted
    RunSafeSequencer   = 1, // "safe" is rx_safestate_sequencer reading rx_safe_sequencer_state
};

// ── Request-stream config (extraction §3.8) — batch B ────────────────────────
// Left exactly as this codebase's own pre-rewrite v2.x design already had
// it; rcp/e2e.hpp, rcp/watchdog.hpp, rcp/sim.hpp, and their own tests
// already depend on this exact shape. c-RCP's own request-stream-cfg wire
// codec (rcp_regmap_request_stream_cfg_render()/_apply_reconfig(), and its
// own documented TC18 0.5.1_RC5 terminology-drift reconciliation) is batch
// B's job.

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
};

// ── EP-ID / byte_bus_id mapping table (extraction §3.9) — batch B ────────────
// Left exactly as this codebase's own pre-rewrite v2.x design already had
// it. c-RCP's own rcp_regmap_ep_id_map_entry_t additionally carries a
// request_stream_index field this struct does not yet have — needed for a
// real rcp_regmap_ep_id_map_is_valid_association() (see writer_ctx() below,
// which fails closed until that lands) — batch B's job.

struct EpIdMappingEntry {
    EndpointId      ep_id       = 0;
    avtp::ByteBusId byte_bus_id = 0;
};

// ── Response / ack queue config (extraction §3.10) — batch B ─────────────────

struct ResponseQueueConfig {
    uint16_t response_queue_size = 0;
    uint16_t ack_queue_size      = 0;
    uint32_t flush_time = 0;
};

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
    uint16_t svr_sequencer_state_ptr   = 0; // REQ-RMAP-038 (Table 20, 0x002E, 16 bit, R): address of the Sequencer_config register map (§12.7.10, batch B)

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
// TODO(phase4-batch-b): TC18 §12.3.1.2's own "any valid stream_id/
// byte_bus_id combination is accepted, but only when no root client is
// configured at all" rule needs a real EP-ID/byte_bus_id association check
// (c-RCP's rcp_regmap_ep_id_map_is_valid_association(), keyed off each
// row's own request_stream_index) to evaluate for real. EpIdMappingEntry
// (batch B's own type, left unchanged above) does not yet carry that
// field, so via_valid_stream_association is conservatively, always false
// here -- fail-closed (never a false "yes") until batch B lands the full
// association table. requesting_byte_bus_id/ep_id_map/ep_id_map_count are
// already accepted (matching c-RCP's own signature exactly) so batch B's
// own fix is a pure function-body change, not a signature change.
inline lifecycle::WriterCtx writer_ctx(const GeneralMap& map, const EpClient* ep_client,
                                        uint16_t requesting_stream_index, bool via_ep0, bool via_unicast,
                                        bool via_discovery_stream,
                                        avtp::ByteBusId requesting_byte_bus_id,
                                        const EpIdMappingEntry* ep_id_map, size_t ep_id_map_count) noexcept {
    (void)requesting_byte_bus_id;
    (void)ep_id_map;
    (void)ep_id_map_count;

    lifecycle::WriterCtx ctx;

    ctx.via_root_client_ep0 = via_ep0 && map.svr_root_client_index != kNoRootClient &&
                              requesting_stream_index == map.svr_root_client_index;

    ctx.via_owning_stream = ep_client != nullptr && ep_client->has_owning_stream &&
                            requesting_stream_index == ep_client->owning_stream_index;

    ctx.via_non_unicast_frame = !via_unicast;

    // REQ-RMAP-070: pass through, matching via_ep0/via_unicast's own
    // already-classified-input convention.
    ctx.via_discovery_stream = via_discovery_stream;

    // See this function's own TODO(phase4-batch-b) doc comment above.
    ctx.via_valid_stream_association = false;

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
// describe. general/svr_ep_cfg are this batch's own GeneralMap/SvrEpCfg
// content (see above); the pointer/capacity fields and table-content
// vectors below them are batch B's unchanged, pre-existing shape.

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

// Enable std::error_code construction from rcp::regmap::RegMapErrc/
// GeneralMapErrc/EpGenericCfgReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::regmap::RegMapErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::GeneralMapErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::regmap::EpGenericCfgReconfigErrc> : true_type {};
} // namespace std
