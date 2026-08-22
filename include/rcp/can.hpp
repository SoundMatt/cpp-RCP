// fusa:req REQ-CANEP-001
// fusa:req REQ-CANEP-002
// fusa:req REQ-CANEP-003
// fusa:req REQ-CANEP-004
// fusa:req REQ-CANEP-005
// fusa:req REQ-CANEP-006
// fusa:req REQ-CANEP-007
// fusa:req REQ-CANEP-008
// fusa:req REQ-CANEP-009
// fusa:req REQ-CANEP-010
// fusa:req REQ-CANEP-011
// fusa:req REQ-CANEP-012
// fusa:req REQ-CANEP-013
// fusa:req REQ-CANEP-014
// fusa:req REQ-CANEP-015
// fusa:req REQ-CANEP-016
// fusa:req REQ-CANEP-017
// fusa:req REQ-CANEP-018
// fusa:req REQ-CANEP-019
// fusa:req REQ-CANEP-020
// fusa:req REQ-CANEP-021
// fusa:req REQ-CANEP-022
// fusa:req REQ-CANEP-023
// fusa:req REQ-CANEP-024
// fusa:req REQ-CANEP-025
// fusa:req REQ-CANEP-026
// fusa:req REQ-CANEP-027
// fusa:req REQ-CANEP-028
// fusa:req REQ-CANEP-029
// fusa:req REQ-CANEP-030
// fusa:req REQ-CANEP-031
// fusa:req REQ-CANEP-032
// fusa:req REQ-CANEP-033
// fusa:req REQ-CANEP-034
// fusa:req REQ-CANEP-035
// fusa:req REQ-CANEP-036
// fusa:req REQ-CANEP-037
// fusa:req REQ-CANEP-038
// fusa:req REQ-CANEP-039
// fusa:req REQ-CANEP-040
// fusa:req REQ-CANEP-041
// fusa:req REQ-CANEP-042

// CAN controller endpoint (ep_type 0x0B) — the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC's Classical/FD/XL frame-format
// selection, data-frame-only transfer model, CAN XL's extra header region
// and extended payload ceiling, the three per-phase bit-timing register
// sets, CAN-XL-specific acceptance/receive filtering, and CAN XL multi-frame
// fragmentation (extraction §5.11, §7, §13.7.11.3).
//
// ROADMAP.md "Phase 17" (cpp-RCP issue #129), Phase 3 ("Per-endpoint
// modules"): ported from c-RCP's include/rcp/ep_can.h + src/ep_can.c, this
// project's RC5-spec-conformant reference implementation for this endpoint
// type — including issues #610/#611/#614/#616, all fixed on the c-RCP side,
// which wired CAN XL multi-frame fragmentation via c-RCP's fragment.h. No
// spec prose, bit layout, or numeric constant is reproduced here.
//
// ── What this pass changed relative to this file's pre-rewrite content ────
//
// The pre-rewrite version of this header (ROADMAP.md milestone 51, v2.7.0)
// was explicitly validation-only: identifier range checks, frame-format
// payload ceilings, and XL acceptance filtering, with "no ACF-level wire
// encode/decode for CAN requests/responses anywhere in this codebase" (see
// ROADMAP.md's v2.24.0 investigation note) — a deliberate no-go on
// fragmentation (milestone 52) left CAN XL's real worst-case payload
// (2048 data octets, RCP_EP_CAN_XL_MAX_DATA_LEN on the c-RCP side)
// unreachable in a single AVTPDU. This pass closes that gap, porting:
//
//   - FrameFormat: widened from this header's old 3-value
//     {Classical,Fd,Xl} to c-RCP's real 6-value Table 57 set (Cbff/Ceff/
//     Fbff/Feff/XlClassicalPl/XlNewPl), each with its own TC18-defined
//     11-/29-bit arbitration-id width (frame_format_id_width()) — the old
//     header modeled id width via a standalone CanIdentifier::extended
//     flag independent of frame format, which could disagree with the
//     format actually selected (e.g. format=Classical with
//     extended=true); c-RCP's own real design derives id width from
//     frame_format alone, so CanIdentifier is retired in favor of a plain
//     arbitration_id validated against frame_format_id_width(format) —
//     see arbitration_id_valid().
//   - The real ACF-level wire codec (encode_frame_request()/
//     decode_frame_request()/encode_frame_response()/
//     decode_frame_response()), TC18 §13.7.11.3 Figure 39's
//     frame_format+arbitration_id leading quadlet, followed — only for a
//     CAN XL format — by xl_header's sdt/vcid/af, followed by the raw CAN
//     data bytes. RRS is not separately encoded; its value is implied by
//     frame_format alone (present iff the format is a CAN XL variant) —
//     see XlHeader's own comment.
//   - CAN XL fragmentation (frame_request_fragment_count()/
//     encode_frame_request_fragmented(), frame_response_fragment_count()/
//     encode_frame_response_fragmented()/decode_frame_response_fragment()/
//     decode_reassembled_frame_response()), wired onto rcp/fragment.hpp's
//     plan_count()/plan()/Reassembler — see "Fragmentation wiring" below.
//   - CanFunctionalConfig (three independent bit-timing register sets,
//     delay compensation, the execution-delay clock divider, the
//     RCP_EP_CAN_XL_MAX_FILTERS-deep XL acceptance/ID filter table, and
//     the TC18 §13.7.11.2 Table 56 EP_func register block —
//     render_registers()/apply_reconfig()), all lifecycle-gated the same
//     way every c-RCP endpoint type's functional config is
//     (rcp::lifecycle::field_writable(), FieldKind::FunctionalW). CAN's
//     own former ungated configure_bit_timing()/bit_timing() pair is
//     retired in favor of this properly-gated surface; CanEndpoint's own
//     receive() now matches an incoming CAN XL frame against
//     CanFunctionalConfig::xl_filters (c-RCP's real, register-modeled
//     filter table) instead of a separate, ungated xl_receive_filters_
//     vector this header used to carry as its own invention.
//
// What this pass deliberately did NOT change: CanEndpoint::transmit()/
// receive()/handle_request()/last_transmitted()/last_received() keep their
// exact pre-existing signatures — rcp/mock.hpp's dispatch_can() already
// calls `can_.handle_request(req.evt_op, frame)` with a `can::CanDataFrame`
// built directly from a decoded ACF payload, and this pass does not touch
// rcp/mock.hpp at all (wiring the new ACF wire codec and fragmentation into
// mock.hpp's own dispatch loop is ROADMAP.md Phase 17 item 4,
// "Server/dispatch", not this pass's "per-endpoint modules" scope). CanErrc's
// four pre-existing enumerator names/values (identifier_out_of_range=1,
// payload_exceeds_format_limit=2, xl_payload_exceeds_single_avtpdu_bound=3,
// config_write_not_supported=4) are therefore also kept unchanged, since
// rcp/mock.hpp's wire_error_code_for() compares against them by name.
//
// ── Fragmentation wiring (the centerpiece of this pass) ──────────────────
//
// A worst-case CAN XL write's combined prefix-then-data payload
// (kArbitrationPrefixLen + kXlHeaderLen + kXlMaxDataLen = 4 + 6 + 2048 =
// 2058 octets, kXlMaxEncodedLen — the same figure c-RCP issue #614/#616's
// own writeup cites, and the exact figure rcp/fragment.hpp's own
// kDefaultReassemblyCapacity (4096) was sized with headroom over) does not
// fit within a single ACF message (rcp::acf::kAcfAbbMaxPayload, 2036 octets)
// — the real gap ep_can.h's own file header documents as the concrete
// driver for c-RCP's fragmentation go-decision. encode_frame_request_
// fragmented()/encode_frame_response_fragmented() below build the combined
// payload into a fixed kXlMaxEncodedLen-byte stack buffer (no heap
// allocation — matching c-RCP issue #521's own fixed-capacity push), call
// rcp::fragment::plan_count()/plan() to split it into at most
// kMaxFragmentSegments (256, ported from c-RCP's own
// RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS — see that constant's own comment for
// why this ceiling is realistic) ACF frames, and encode each one with the
// ms flag and read_size_or_segment_num field rcp::fragment::Segment
// prescribes. decode_frame_response_fragment() decodes one such fragment's
// ms/segment_num/payload without assuming it holds the whole leading
// quadlet (a caller feeds these three values straight into a
// rcp::fragment::Reassembler); once the Reassembler reports
// ReasmResult::kComplete, decode_reassembled_frame_response() applies
// Figure 39's leading-quadlet-then-data parsing to the fully reassembled
// buffer — the same function unfragmented single-frame decoding uses
// internally.
//
// ── The oversized-reassembly lesson (c-RCP issues #614/#616), and why this
// module does not re-fix it here ───────────────────────────────────────────
//
// rcp/fragment.hpp's own header comment already documents this lesson in
// full: a reassembled payload can be genuinely, correctly complete
// (ReasmResult::kComplete, Reassembler::data()/size() fully populated) and
// still be too large to re-encode into a single response frame — the fix
// for that is not in the fragmentation primitive (nor in this endpoint
// module) but in the *caller* that reassembles a fragmented request and
// must check the result against a frame-format ceiling BEFORE attempting to
// re-encode it, building a real error response rather than silently
// dropping the request when that check fails. That caller, for this
// endpoint type, is rcp/mock.hpp's own dispatch loop (or any other future
// dispatch-loop caller) — explicitly out of this pass's scope, per the note
// above. This module's own contribution to closing that gap is making sure
// decode_reassembled_frame_response() never truncates or silently accepts
// an over-length reassembled buffer itself: it validates the leading
// quadlet and frame-format-dependent prefix length exactly as the
// unfragmented decoder does, and returns *out_rx_data/*out_rx_len as the
// full, true reassembled data length for the caller's own ceiling check.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete bit-timing
// register field layout and the general (non-XL) acceptance-filter bank
// (CanAcceptanceFilter/acceptance_filter_matches(), which has no c-RCP
// equivalent — see its own comment) are this implementation's own, same as
// the equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp, rcp/regmap.hpp,
// rcp/endpoint.hpp, rcp/fragment.hpp, and rcp/lin.hpp.
//
// TODO(phase3-followup): same as rcp/lin.hpp's own TODO — once
// rcp/regmap.hpp's functional-config split is re-derived from c-RCP
// (ROADMAP.md Phase 17 item 4), recompose CanFunctionalConfig's five
// ep_enable/ep_clear_req_storage/ep_req_crc_enable/ep_response_ts_enable/
// ep_suppress_response flags on top of that shared struct instead of
// carrying local duplicates of them here. Separately (matching c-RCP's own
// still-open finding): TC18's own Table 56 acceptance-filter/receive-filter
// address region (0x0024 onward) has a genuine, unresolved address-collision
// defect in the primary source — this module, like c-RCP's own
// rcp_ep_can_render_registers()/_apply_reconfig(), deliberately stops its
// own register-block model at 0x0024 (kEpFuncLen) and does not attempt to
// serialize that region at all.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace can {

// ── Frame format (TC18 §13.7.11.3 Figure 39 / Table 57) ─────────────────────
// Packed into the request/response's own leading payload quadlet — NOT into
// evt[2:0], which for CAN (a member of TC18 §13.5 Table 33's {ADC, PWM_IN,
// I2C, LIN, CAN, UART, ISELED, MDIO} row) has the same ordinary meaning
// every other endpoint in that row gives it (see CanEndpoint::handle_request
// below). Ported from rcp_ep_can_frame_format_t.

enum class FrameFormat : uint8_t {
    Cbff          = 0, // Classical Base Frame Format, 11-bit id
    Ceff          = 1, // Classical Extended Frame Format, 29-bit id
    Fbff          = 2, // FD Base Frame Format, 11-bit id
    Feff          = 3, // FD Extended Frame Format, 29-bit id
    XlClassicalPl = 4, // XL frame, classical CAN physical layer
    XlNewPl       = 5, // XL frame, new CAN XL physical layer
};

enum class IdWidth : uint8_t { Base11 = 0, Extended29 = 1 };

constexpr size_t kClassicalMaxDataLen = 8;
constexpr size_t kFdMaxDataLen        = 64;
constexpr size_t kXlMaxDataLen        = 2048;

// True iff v (a raw wire value) selects one of the six defined frame
// formats, i.e. v <= 5. Values 6 and 7 select no defined format. Ported
// from rcp_ep_can_frame_format_valid().
constexpr bool frame_format_valid(uint8_t v) noexcept {
    return v <= static_cast<uint8_t>(FrameFormat::XlNewPl);
}

// True iff format is one of the two CAN XL variants. Ported from
// rcp_ep_can_frame_format_is_xl().
constexpr bool frame_format_is_xl(FrameFormat format) noexcept {
    return format == FrameFormat::XlClassicalPl || format == FrameFormat::XlNewPl;
}

// EXTENDED_29 for CEFF/FEFF, BASE_11 for every other defined format
// (CBFF, FBFF, and both CAN XL variants — CAN XL's own arbitration-phase
// identifier is a base-width Priority ID, independent of physical-layer
// selection). An invalid format value fails safe to BASE_11. Ported from
// rcp_ep_can_frame_format_id_width().
constexpr IdWidth frame_format_id_width(FrameFormat format) noexcept {
    switch (format) {
    case FrameFormat::Ceff:
    case FrameFormat::Feff: return IdWidth::Extended29;
    default:                return IdWidth::Base11;
    }
}

// True iff id is in range for format's own id width (<= 0x7FF for
// Base11, <= 0x1FFFFFFF for Extended29); always false for an invalid
// format value. Ported from rcp_ep_can_arbitration_id_valid().
constexpr bool arbitration_id_valid(FrameFormat format, uint32_t id) noexcept {
    if (!frame_format_valid(static_cast<uint8_t>(format))) return false;
    return frame_format_id_width(format) == IdWidth::Extended29 ? id <= 0x1FFFFFFFu : id <= 0x7FFu;
}

// The largest raw CAN data length (octets) format's own frame kind permits:
// 8 (CBFF/CEFF), 64 (FBFF/FEFF), or kXlMaxDataLen (2048, both XL variants);
// 0 for an invalid format value. Ported from
// rcp_ep_can_frame_format_max_data_len().
constexpr size_t frame_format_max_data_len(FrameFormat format) noexcept {
    switch (format) {
    case FrameFormat::Cbff:
    case FrameFormat::Ceff:          return kClassicalMaxDataLen;
    case FrameFormat::Fbff:
    case FrameFormat::Feff:          return kFdMaxDataLen;
    case FrameFormat::XlClassicalPl:
    case FrameFormat::XlNewPl:       return kXlMaxDataLen;
    default:                         return 0;
    }
}

// REQ-CANEP-038: the endpoint-level constraint that a frame's own XL variant
// must agree with the endpoint's provisioned physical layer
// (xl_new_pl_provisioned, CanFunctionalConfig) — a non-XL frame trivially
// matches, since it carries no PL choice of its own to conflict with. Ported
// from rcp_ep_can_xl_frame_matches_provisioned_pl(). Like c-RCP's own
// version, this is exposed as a standalone validator a caller MAY use — it
// is not automatically enforced by transmit()/receive()/handle_request()
// below (wiring it into a real dispatch path is Phase 4 scope, same as the
// fragmentation-into-mock.hpp note above).
constexpr bool xl_frame_matches_provisioned_pl(bool xl_new_pl_provisioned,
                                                FrameFormat format) noexcept {
    if (!frame_format_is_xl(format)) return true;
    return xl_new_pl_provisioned ? (format == FrameFormat::XlNewPl)
                                  : (format == FrameFormat::XlClassicalPl);
}

// ── CAN XL's extra header fields (SDT/VCID/AF) ───────────────────────────────
// Only meaningful (and only ever populated on decode) when the associated
// frame_format is XlClassicalPl or XlNewPl. RRS (Remote Request
// Substitution) is deliberately not a member here: its value is implied
// entirely by frame_format itself (present, at a fixed value, whenever
// frame_format is a CAN XL variant; absent otherwise) — this module never
// carries it as a separately encoded field. Ported from
// rcp_ep_can_xl_header_t.
struct XlHeader {
    uint8_t  sdt  = 0; // SDU Type
    uint8_t  vcid = 0; // Virtual CAN Network ID
    uint32_t af   = 0; // Acceptance Field
};

constexpr size_t kArbitrationPrefixLen = 4; // frame_format(3 bits) + arbitration_id(29 bits), one quadlet
constexpr size_t kXlHeaderLen          = 6; // sdt(1) + vcid(1) + af(4)

// Worst-case combined prefix-then-data payload this module's own encode
// functions can produce for a CAN XL frame request/response — ported from
// RCP_EP_CAN_XL_MAX_ENCODED_LEN (4 + 6 + 2048 = 2058).
constexpr size_t kXlMaxEncodedLen = kArbitrationPrefixLen + kXlHeaderLen + kXlMaxDataLen;

// The largest CAN XL data length whose combined prefix-then-data payload
// still fits within a single (unfragmented) ACF message
// (rcp::acf::kAcfAbbMaxPayload, 2036 octets) — 2036 - 10 = 2026. This is the
// real TC18/ACF capability gap ep_can.h's own file header documents (a
// worst-case CAN XL frame does not fit in one ACF message at all): a
// CanDataFrame whose data falls between this bound and kXlMaxDataLen is
// structurally valid (accepted by frame_format_max_data_len()'s own
// ceiling) but not representable by encode_frame_request()/
// encode_frame_response() alone — a caller must use
// encode_frame_request_fragmented()/encode_frame_response_fragmented()
// instead. validate_frame() below reports this specific condition as
// CanErrc::xl_payload_exceeds_single_avtpdu_bound.
constexpr size_t kMaxXlPayloadSingleFrame = acf::kAcfAbbMaxPayload - kArbitrationPrefixLen - kXlHeaderLen;

// The largest number of ACF frames this module's own fragmented encoders
// will produce — ported from c-RCP issue #521's own
// RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS: kXlMaxEncodedLen (2058) bytes is this
// module's hard compile-time ceiling on the combined payload being
// fragmented, so a genuinely unbounded fragment count is not possible; 256
// is this module's own realistic ceiling on top of that, chosen so a
// max_fragment_payload as small as ~8 octets is still representable. A
// caller configuring a smaller max_fragment_payload gets an empty result
// from the fragment_count()/fragmented-encode functions below rather than a
// silently truncated plan.
constexpr size_t kMaxFragmentSegments = 256;

// This module's own chosen CAN-XL acceptance/ID filter-table depth — not a
// spec-derived number (TC18's own Table 56 acceptance-filter region has a
// genuine, unresolved address-collision defect — see this file's own header
// comment). Ported from RCP_EP_CAN_XL_MAX_FILTERS.
constexpr uint8_t kMaxXlFilters = 4;

// ── Errors ────────────────────────────────────────────────────────────────────
// identifier_out_of_range/payload_exceeds_format_limit/
// xl_payload_exceeds_single_avtpdu_bound/config_write_not_supported (values
// 1-4) are unchanged from this header's pre-existing content — rcp/mock.hpp's
// wire_error_code_for() compares against all four by name.
// bad_frame_format..bad_arbitration_id (5-10) are new, ported from
// rcp_ep_can_errc_t (RCP_EP_CAN_ERR_*), for the new wire-codec functions
// below.

enum class CanErrc : int {
    identifier_out_of_range                = 1, // arbitration_id exceeds its frame-format-derived 11-/29-bit range
    payload_exceeds_format_limit           = 2, // payload length exceeds the selected FrameFormat's own max
    // Within the format's own ceiling, but the combined prefix-then-data
    // payload does not fit within a single (unfragmented) ACF message — see
    // kMaxXlPayloadSingleFrame above. A caller must use the *_fragmented()
    // functions below instead.
    xl_payload_exceeds_single_avtpdu_bound = 3,
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). CanEndpoint::handle_request deliberately does not
    // implement the configuration-write shape end to end (that needs
    // dispatch-level wiring to CanFunctionalConfig/apply_reconfig() below,
    // Phase 4 scope) — reported explicitly rather than silently accepted as
    // a plain transmit or silently ignored.
    config_write_not_supported = 4,
    bad_frame_format            = 5, // ported from RCP_EP_CAN_ERR_BAD_FRAME_FORMAT
    short_frame                 = 6, // ported from RCP_EP_CAN_ERR_SHORT_FRAME
    bad_msg_type                 = 7, // ported from RCP_EP_CAN_ERR_BAD_MSG_TYPE
    wrong_bus                    = 8, // ported from RCP_EP_CAN_ERR_WRONG_BUS
    wrong_op                     = 9, // ported from RCP_EP_CAN_ERR_WRONG_OP
    bad_evt                      = 10, // ported from RCP_EP_CAN_ERR_BAD_EVT
    bad_arbitration_id           = 11, // ported from RCP_EP_CAN_ERR_BAD_ARBITRATION_ID
};

inline const std::error_category& can_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.can"; }
        std::string message(int ev) const override {
            switch (static_cast<CanErrc>(ev)) {
            case CanErrc::identifier_out_of_range:
                return "rcp/can: arbitration_id exceeds its frame-format-derived 11-/29-bit range";
            case CanErrc::payload_exceeds_format_limit:
                return "rcp/can: payload exceeds the selected FrameFormat's payload limit";
            case CanErrc::xl_payload_exceeds_single_avtpdu_bound:
                return "rcp/can: CAN XL payload requires fragmentation — use the *_fragmented functions";
            case CanErrc::config_write_not_supported:
                return "rcp/can: evt[2:0]=111b configuration-write requests are not yet implemented";
            case CanErrc::bad_frame_format:  return "rcp/can: invalid frame format";
            case CanErrc::short_frame:       return "rcp/can: frame too short";
            case CanErrc::bad_msg_type:      return "rcp/can: unexpected ACF message type";
            case CanErrc::wrong_bus:         return "rcp/can: wrong byte_bus_id";
            case CanErrc::wrong_op:          return "rcp/can: wrong ACF op";
            case CanErrc::bad_evt:           return "rcp/can: evt[2:0] is not 0b000";
            case CanErrc::bad_arbitration_id: return "rcp/can: arbitration_id out of range for frame format";
            default:
                return "rcp/can: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(CanErrc e) noexcept {
    return {static_cast<int>(e), can_category()};
}

inline std::error_code validate_identifier(FrameFormat format, uint32_t arbitration_id) noexcept {
    if (!arbitration_id_valid(format, arbitration_id)) return make_error_code(CanErrc::identifier_out_of_range);
    return {};
}

// ── Data frame (no remote-frame shape) ───────────────────────────────────────
// TC18's own text states outright that sending remote frames is not
// supported — there is no wire representation, encode function, or decode
// outcome for a remote frame anywhere in this module.

struct CanDataFrame {
    FrameFormat           format         = FrameFormat::Cbff;
    uint32_t              arbitration_id = 0;
    XlHeader               xl_header{};    // meaningful only when frame_format_is_xl(format)
    std::vector<uint8_t>   data;
};

// validate_frame checks arbitration_id against format's own id width, then
// the payload against the selected format's ceiling — for a CAN XL format
// specifically distinguishing "exceeds the format's own ceiling" from
// "exceeds what a single unfragmented ACF message can carry" so callers can
// tell a genuinely invalid frame from one that needs
// encode_frame_request_fragmented()/encode_frame_response_fragmented()
// instead of encode_frame_request()/encode_frame_response().
inline std::error_code validate_frame(const CanDataFrame& f) noexcept {
    auto ec = validate_identifier(f.format, f.arbitration_id);
    if (ec) return ec;

    if (f.data.size() > frame_format_max_data_len(f.format))
        return make_error_code(CanErrc::payload_exceeds_format_limit);

    if (frame_format_is_xl(f.format) && f.data.size() > kMaxXlPayloadSingleFrame)
        return make_error_code(CanErrc::xl_payload_exceeds_single_avtpdu_bound);

    return {};
}

// ── Per-phase bit-timing register sets ───────────────────────────────────────
// Separate register sets for the arbitration phase (used by every frame
// format for at least its identifier/arbitration field) and the FD/XL
// data-phase bit rates, which run faster than arbitration once BRS is set.
// CAN XL's data phase is its own third register set, distinct from FD's.
// Standard, publicly documented Bosch-CAN-style bit-timing register
// concepts, not values taken from the specification — ported from
// rcp_ep_can_bit_timing_t (field widths, including sync_jump_width's 8-bit
// width, match exactly).

struct CanBitTimingPhase {
    uint32_t prescaler       = 0;
    uint16_t prop_seg        = 0;
    uint16_t phase_seg1      = 0;
    uint16_t phase_seg2      = 0;
    uint8_t  sync_jump_width = 0;
};

struct CanBitTimingConfig {
    CanBitTimingPhase arbitration; // Classical's only phase; FD/XL's arbitration-phase register set
    CanBitTimingPhase fd_data;     // FD's data-phase register set
    CanBitTimingPhase xl_data;     // XL's data-phase register set, independent of fd_data
};

// ── Functional config ─────────────────────────────────────────────────────────
// Ported from rcp_ep_can_functional_cfg_t — see this file's own header
// comment for why the five Table 35 "common" flags are local members here
// rather than composed from rcp/regmap.hpp's EndpointFunctionalConfig.

struct CanXlFilter {
    uint32_t id     = 0;
    uint32_t mask   = 0;
    bool     enable = false;
};

struct CanFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    CanBitTimingConfig timing;
    bool                delay_comp_enable       = false;
    uint8_t             delay_comp_offset       = 0;
    uint32_t            exec_delay_clk_divider  = 0; // execution-delay timing only — NOT bit timing
    std::array<CanXlFilter, kMaxXlFilters> xl_filters{};
    uint16_t             ep_status               = 0; // can_ep_status, Table 56 0x0006
    uint32_t             status                   = 0; // CAN EP status, Table 56 0x001C
    uint32_t             fifo_status              = 0; // FIFO status, Table 56 0x0020
    // "usage of new PL (YES|NO) for CAN XL", §13.7.11.2 — in-memory only, no
    // wire offset (TC18 gives this setting no register offset anywhere in
    // Table 56 — a genuine specification gap, not a local implementation
    // one). See xl_frame_matches_provisioned_pl() above.
    bool xl_new_pl_provisioned = false;
};

// True iff index is a valid filter-table index (0..kMaxXlFilters-1). Ported
// from rcp_ep_can_xl_filter_index_valid().
constexpr bool xl_filter_index_valid(uint8_t index) noexcept { return index < kMaxXlFilters; }

// functional_cfg_writable is a thin, named wrapper over
// rcp::lifecycle::field_writable() with FieldKind::FunctionalW — ported
// from rcp_ep_can_functional_cfg_writable().
inline bool functional_cfg_writable(lifecycle::ServerState state,
                                     lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_arbitration_timing(CanFunctionalConfig& cfg, CanBitTimingPhase timing,
                                    lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.timing.arbitration = timing;
    return true;
}

inline bool set_fd_data_timing(CanFunctionalConfig& cfg, CanBitTimingPhase timing,
                                lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.timing.fd_data = timing;
    return true;
}

inline bool set_xl_data_timing(CanFunctionalConfig& cfg, CanBitTimingPhase timing,
                                lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.timing.xl_data = timing;
    return true;
}

// One setter for both delay_comp_enable/delay_comp_offset (always
// reconfigured as a pair on the wire).
inline bool set_delay_compensation(CanFunctionalConfig& cfg, bool enable, uint8_t offset,
                                    lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.delay_comp_enable = enable;
    cfg.delay_comp_offset = offset;
    return true;
}

inline bool set_xl_new_pl_provisioned(CanFunctionalConfig& cfg, bool new_pl_provisioned,
                                       lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.xl_new_pl_provisioned = new_pl_provisioned;
    return true;
}

inline bool set_exec_delay_clk_divider(CanFunctionalConfig& cfg, uint32_t divider,
                                        lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.exec_delay_clk_divider = divider;
    return true;
}

// Sets cfg.xl_filters[index] iff index is xl_filter_index_valid() and
// functional_cfg_writable() authorizes the write; returns whether the write
// was applied. cfg is left entirely unchanged otherwise.
inline bool set_xl_filter(CanFunctionalConfig& cfg, uint8_t index, CanXlFilter filter,
                           lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!xl_filter_index_valid(index)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.xl_filters[index] = filter;
    return true;
}

// ── CAN's own general (non-XL) acceptance-filter bank ────────────────────────
// This bank — and acceptance_filter_matches()/CanEndpoint::receive()'s use
// of it below for Classical/FD frames — has NO c-RCP equivalent: c-RCP's
// own functional config models only the CAN-XL-specific xl_filters table
// above. This is this implementation's own extension, kept from this
// header's pre-existing content, same disclaimer as this file's own header
// comment.

struct CanAcceptanceFilter {
    uint32_t id       = 0;
    uint32_t mask     = 0;
    bool     extended = false; // matches only a frame whose own frame_format_id_width() agrees
};

inline bool acceptance_filter_matches(const CanAcceptanceFilter& filt, FrameFormat format,
                                       uint32_t arbitration_id) noexcept {
    if (filt.extended != (frame_format_id_width(format) == IdWidth::Extended29)) return false;
    return (arbitration_id & filt.mask) == (filt.id & filt.mask);
}

// ── The EP_func register block (the evt[2:0] == 111b target), TC18
// §13.7.11.2 Table 56 ────────────────────────────────────────────────────────
//
//   0x0000  can_ep_len               8 bit  R    kEpFuncLen (0x24)
//   0x0001  Reserved                 8 bit  R    reads 0x00
//   0x0002  can_ep_enable&clr        8 bit  R/W  Table 35 common entries
//   0x0003  can_ep_options           8 bit  R/W* Table 35 common entries
//   0x0004  can_base_clk            16 bit  R    CAN system clock (always 0
//                                                  — no real clock modelled)
//   0x0006  can_ep_status           16 bit  R/W
//   0x0008-0x001B  clk_divider, two reserved octets, and the three "CAN bit
//                  time register" fields plus TDCC — TC18 gives these
//                  32-bit registers no sub-field bit-layout anywhere in the
//                  specification text (this file's own header comment,
//                  ported from ep_can.h's identical finding), so this span
//                  is treated as one contiguous read-only region rather
//                  than inventing an unverified bit-packing scheme.
//   0x001C  status                  32 bit  R/W
//   0x0020  fifo_status             32 bit  R/W
//
// closing at kEpFuncLen (0x0024), immediately before Table 56's own
// acceptance-filter region — see this file's own header comment for why
// this module's register-block model deliberately stops there.

constexpr uint16_t kRegEpLen         = 0x0000;
constexpr uint16_t kRegReserved01    = 0x0001;
constexpr uint16_t kRegEpEnableClr   = 0x0002;
constexpr uint16_t kRegEpOptions     = 0x0003;
constexpr uint16_t kRegBaseClk       = 0x0004;
constexpr uint16_t kRegEpStatus      = 0x0006;
constexpr uint16_t kRegUndecomposedStart = 0x0008;
constexpr uint16_t kRegUndecomposedLen   = 0x0014; // 0x0008..0x001B
constexpr uint16_t kRegStatus        = 0x001C;
constexpr uint16_t kRegFifoStatus    = 0x0020;

constexpr size_t kEpFuncLen       = 0x0024;
constexpr size_t kReconfigAddrLen = 2;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = (1u << 0);
// Table 35 fixes ep_clear_req_storage at bit 4 for every endpoint type
// (c-RCP issue #470 fixed CAN's own register block from an earlier,
// wrong (1u<<1) value — this port uses the correct bit position from the
// start).
constexpr uint8_t kEnableClrBitClear  = (1u << 4);
constexpr uint8_t kOptionsBitReqCrc   = (1u << 0);
constexpr uint8_t kOptionsBitRespTs   = (1u << 3);
constexpr uint8_t kOptionsBitSuppress = (1u << 7);

inline void put_u16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[1] = static_cast<uint8_t>(v & 0xFFu);
}
inline uint16_t get_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline void put_u32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<uint8_t>(v & 0xFFu);
}
inline uint32_t get_u32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline bool reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kRegEpLen || addr == kRegReserved01 ||
           (addr >= kRegBaseClk && addr < kRegBaseClk + 2u) ||
           (addr >= kRegUndecomposedStart && addr < kRegUndecomposedStart + kRegUndecomposedLen);
}
} // namespace detail

// Serializes cfg's EP_func registers into out[0..kEpFuncLen) exactly as a
// configuration *read* of the whole block would report them. Ported from
// rcp_ep_can_render_registers().
inline void render_registers(const CanFunctionalConfig& cfg,
                              std::array<uint8_t, kEpFuncLen>& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;

    out[kRegEpLen]       = static_cast<uint8_t>(kEpFuncLen);
    out[kRegReserved01]  = 0;
    out[kRegEpEnableClr] = enable_clr;
    out[kRegEpOptions]   = options;
    detail::put_u16(&out[kRegBaseClk], 0); // no real clock source modelled
    detail::put_u16(&out[kRegEpStatus], cfg.ep_status);
    std::fill(out.begin() + kRegUndecomposedStart,
              out.begin() + kRegUndecomposedStart + kRegUndecomposedLen, uint8_t{0});
    detail::put_u32(&out[kRegStatus], cfg.status);
    detail::put_u32(&out[kRegFifoStatus], cfg.fifo_status);
}

enum class CanReconfigErrc : int {
    short_payload = 1, // payload carries no address prefix, or an address prefix with no data octet after it
    out_of_range  = 2, // start_address + data length exceeds kEpFuncLen — the whole write is ignored
};

inline const std::error_category& can_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.can.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<CanReconfigErrc>(ev)) {
            case CanReconfigErrc::short_payload: return "rcp/can: CAN configuration write has no address and data";
            case CanReconfigErrc::out_of_range:  return "rcp/can: CAN configuration write extends past the EP_func block";
            default: return "rcp/can: CAN unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(CanReconfigErrc e) noexcept {
    return {static_cast<int>(e), can_reconfig_category()};
}

// Applies the configuration escape hatch (evt[2:0] == 111b) — a 16-bit
// big-endian relative start address followed by configuration data octets.
// Ported from rcp_ep_can_apply_reconfig(). cfg is left entirely unchanged
// on error. Octets landing on a read-only register (EP_LEN, the reserved
// octet, base_clk, and the whole not-yet-decomposed 0x0008-0x001B span) are
// left at their current values while the rest of the span is still applied.
inline std::error_code apply_reconfig(CanFunctionalConfig& cfg, const uint8_t* payload,
                                       size_t payload_len) {
    if (payload_len <= kReconfigAddrLen) return make_error_code(CanReconfigErrc::short_payload);

    const uint16_t start_address = detail::get_u16(payload);
    const size_t   data_len      = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(CanReconfigErrc::out_of_range);

    std::array<uint8_t, kEpFuncLen> block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue; // write ignored
        block[addr] = payload[kReconfigAddrLen + i];
    }

    cfg.ep_enable             = (block[kRegEpEnableClr] & detail::kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (block[kRegEpEnableClr] & detail::kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (block[kRegEpOptions] & detail::kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (block[kRegEpOptions] & detail::kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (block[kRegEpOptions] & detail::kOptionsBitSuppress) != 0;
    cfg.ep_status             = detail::get_u16(&block[kRegEpStatus]);
    cfg.status                = detail::get_u32(&block[kRegStatus]);
    cfg.fifo_status           = detail::get_u32(&block[kRegFifoStatus]);

    return {};
}

// ── Wire layout helpers (TC18 §13.7.11.3 Figure 39) ──────────────────────────

namespace detail {
inline size_t prefix_len_for(FrameFormat format) noexcept {
    return frame_format_is_xl(format) ? (kArbitrationPrefixLen + kXlHeaderLen) : kArbitrationPrefixLen;
}

inline void write_prefix(uint8_t* p, FrameFormat format, uint32_t arbitration_id,
                          const std::optional<XlHeader>& xl_header) noexcept {
    const uint32_t combined = (static_cast<uint32_t>(format) << 29) | (arbitration_id & 0x1FFFFFFFu);
    put_u32(p, combined);
    if (frame_format_is_xl(format) && xl_header) {
        p[4] = xl_header->sdt;
        p[5] = xl_header->vcid;
        put_u32(&p[6], xl_header->af);
    }
}

inline FrameFormat read_frame_format(const uint8_t* p) noexcept {
    return static_cast<FrameFormat>((get_u32(p) >> 29) & 0x7u);
}

inline void read_prefix(const uint8_t* p, FrameFormat format, uint32_t& out_arbitration_id,
                         XlHeader& out_xl_header) noexcept {
    out_arbitration_id = get_u32(p) & 0x1FFFFFFFu;
    if (frame_format_is_xl(format)) {
        out_xl_header.sdt  = p[4];
        out_xl_header.vcid = p[5];
        out_xl_header.af   = get_u32(&p[6]);
    }
}

// Validates the shared encode preconditions for a request/response; returns
// false (nothing further should be encoded) iff any precondition fails.
inline bool encode_preconditions_ok(FrameFormat frame_format, uint32_t arbitration_id,
                                     const std::optional<XlHeader>& xl_header,
                                     size_t data_len) noexcept {
    if (!frame_format_valid(static_cast<uint8_t>(frame_format))) return false;
    if (!arbitration_id_valid(frame_format, arbitration_id)) return false;
    if (data_len > frame_format_max_data_len(frame_format)) return false;

    const bool is_xl = frame_format_is_xl(frame_format);
    if (is_xl != xl_header.has_value()) return false;

    return true;
}

// Builds this module's own prefix-then-data layout into a fixed
// kXlMaxEncodedLen-byte buffer — no heap allocation. Every caller below
// already validates data_len against frame_format_max_data_len() via
// encode_preconditions_ok() before calling, so prefix_len + data_len can
// never exceed kXlMaxEncodedLen.
inline size_t build_payload(FrameFormat frame_format, uint32_t arbitration_id,
                             const std::optional<XlHeader>& xl_header, const uint8_t* data,
                             size_t data_len, std::array<uint8_t, kXlMaxEncodedLen>& out_buf) noexcept {
    const size_t prefix_len = prefix_len_for(frame_format);
    write_prefix(out_buf.data(), frame_format, arbitration_id, xl_header);
    if (data_len > 0) std::copy(data, data + data_len, out_buf.begin() + static_cast<long>(prefix_len));
    return prefix_len + data_len;
}
} // namespace detail

// ── Frame request ─────────────────────────────────────────────────────────────
// Ported from rcp_ep_can_encode_frame_request()/_decode_frame_request().

// Encodes an ACF_ABB frame request addressed to byte_bus_id: evt is left
// entirely 0 (plain request), and the payload is Figure 39's layout —
// frame_format+arbitration_id (the leading quadlet), xl_header (only when
// frame_format is a CAN XL variant), and tx_data (the raw CAN data bytes).
// Returns an empty vector if: frame_format is not frame_format_valid();
// arbitration_id is not arbitration_id_valid() for frame_format; tx_data
// exceeds frame_format_max_data_len(frame_format); or xl_header's presence
// does not match frame_format_is_xl(frame_format).
inline std::vector<uint8_t> encode_frame_request(avtp::ByteBusId byte_bus_id, FrameFormat frame_format,
                                                   uint32_t arbitration_id,
                                                   const std::optional<XlHeader>& xl_header,
                                                   const std::vector<uint8_t>& tx_data,
                                                   uint8_t transaction_num) {
    if (!detail::encode_preconditions_ok(frame_format, arbitration_id, xl_header, tx_data.size()))
        return {};

    std::array<uint8_t, kXlMaxEncodedLen> payload{};
    const size_t payload_len =
        detail::build_payload(frame_format, arbitration_id, xl_header, tx_data.data(), tx_data.size(), payload);

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, std::vector<uint8_t>(payload.begin(), payload.begin() + static_cast<long>(payload_len)));
}

// Decodes and validates an ACF-level CAN frame request from b[0..len).
// Fails with CanErrc::short_frame/bad_msg_type/wrong_bus/wrong_op/bad_evt/
// bad_frame_format/bad_arbitration_id — see ep_can.c's own doc comment for
// the exact condition each maps to. On success, every output parameter is
// populated; out_xl_header is populated iff frame_format_is_xl(out_format).
inline std::error_code decode_frame_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                             FrameFormat& out_format, uint32_t& out_arbitration_id,
                                             XlHeader& out_xl_header, std::vector<uint8_t>& out_tx_data,
                                             uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t> payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(CanErrc::short_frame);
    if (ec) return make_error_code(CanErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(CanErrc::wrong_bus);
    if (!hdr.op) return make_error_code(CanErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(CanErrc::bad_evt);

    if (payload.size() < kArbitrationPrefixLen) return make_error_code(CanErrc::short_frame);
    const FrameFormat frame_format = detail::read_frame_format(payload.data());
    if (!frame_format_valid(static_cast<uint8_t>(frame_format))) return make_error_code(CanErrc::bad_frame_format);

    const size_t prefix_len = detail::prefix_len_for(frame_format);
    if (payload.size() < prefix_len) return make_error_code(CanErrc::short_frame);

    detail::read_prefix(payload.data(), frame_format, out_arbitration_id, out_xl_header);
    if (!arbitration_id_valid(frame_format, out_arbitration_id))
        return make_error_code(CanErrc::bad_arbitration_id);

    out_format           = frame_format;
    out_tx_data          = std::vector<uint8_t>(payload.begin() + static_cast<long>(prefix_len), payload.end());
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── Fragmented request (c-RCP issue #611, rcp/fragment.hpp) ──────────────────
// The request-side counterpart of "Fragmented response" below. Unlike the
// response side, a request is never carried as ACF_GBB — every fragment is
// encoded as ACF_ABB, unconditionally.

// The number of ACF frames encode_frame_request_fragmented() would produce
// for this request's combined prefix-then-data payload split into
// fragments of at most max_fragment_payload octets each. Returns 0 under
// the same conditions encode_frame_request() already fails
// encode_preconditions_ok() for, plus fragment::plan_count()'s own
// 0-sentinel conditions, plus kMaxFragmentSegments.
inline size_t frame_request_fragment_count(FrameFormat frame_format, uint32_t arbitration_id,
                                            const std::optional<XlHeader>& xl_header, size_t tx_len,
                                            size_t max_fragment_payload) noexcept {
    if (!detail::encode_preconditions_ok(frame_format, arbitration_id, xl_header, tx_len)) return 0;
    const size_t combined_len = detail::prefix_len_for(frame_format) + tx_len;
    const size_t count        = fragment::plan_count(combined_len, max_fragment_payload);
    if (count > kMaxFragmentSegments) return 0;
    return count;
}

// Encodes a CAN frame write request as one or more ACF_ABB frames,
// fragmenting via rcp::fragment's ms/segment_num mechanism whenever the
// combined prefix-then-data payload exceeds max_fragment_payload octets.
// Every fragment shares byte_bus_id/evt(0)/op(write)/transaction_num —
// frame_format itself lives inside the combined payload's own leading
// quadlet, not per-fragment header state, so only the first fragment
// actually carries it. When the combined payload already fits in one
// fragment, this produces exactly one frame identical to what
// encode_frame_request() itself would have produced — fragmentation is a
// strict superset of the unfragmented path. Returns an empty vector under
// the same conditions frame_request_fragment_count() returns 0 for. This
// function does not itself apply any E2E CRC — a caller wanting E2E
// protection wraps only the final (ms=false) frame after this function
// returns.
inline std::vector<std::vector<uint8_t>>
encode_frame_request_fragmented(avtp::ByteBusId byte_bus_id, FrameFormat frame_format,
                                 uint32_t arbitration_id, const std::optional<XlHeader>& xl_header,
                                 const std::vector<uint8_t>& tx_data, uint8_t transaction_num,
                                 size_t max_fragment_payload) {
    const size_t count = frame_request_fragment_count(frame_format, arbitration_id, xl_header,
                                                        tx_data.size(), max_fragment_payload);
    if (count == 0) return {};

    std::array<uint8_t, kXlMaxEncodedLen> combined{};
    const size_t combined_len =
        detail::build_payload(frame_format, arbitration_id, xl_header, tx_data.data(), tx_data.size(), combined);

    std::array<fragment::Segment, kMaxFragmentSegments> segs{};
    if (fragment::plan(combined_len, max_fragment_payload, segs.data(), count)) return {};

    std::vector<std::vector<uint8_t>> out_frames;
    out_frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id              = byte_bus_id;
        hdr.op                       = true; // write
        hdr.transaction_num          = transaction_num;
        hdr.ms                       = segs[i].ms;
        hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0;

        std::vector<uint8_t> slice(combined.begin() + static_cast<long>(segs[i].offset),
                                    combined.begin() + static_cast<long>(segs[i].offset + segs[i].len));
        out_frames.push_back(acf::encode_acf_abb(hdr, slice));
    }
    return out_frames;
}

// ── Response ───────────────────────────────────────────────────────────────────
// Ported from rcp_ep_can_encode_frame_response()/_decode_frame_response().

// Encodes a CAN frame response with the same frame_format/arbitration_id/
// xl_header/data-validation rules and prefix-then-data payload layout as
// encode_frame_request() (rx_data in place of tx_data), echoing
// transaction_num. Encoded as ACF_ABB when timed is false; as ACF_GBB
// (message_timestamp = timestamp, mtv = true) when timed is true. Returns
// an empty vector under the same conditions encode_frame_request() does.
inline std::vector<uint8_t> encode_frame_response(avtp::ByteBusId byte_bus_id, FrameFormat frame_format,
                                                    uint32_t arbitration_id,
                                                    const std::optional<XlHeader>& xl_header,
                                                    const std::vector<uint8_t>& rx_data,
                                                    uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp) {
    if (!detail::encode_preconditions_ok(frame_format, arbitration_id, xl_header, rx_data.size()))
        return {};

    std::array<uint8_t, kXlMaxEncodedLen> payload{};
    const size_t payload_len =
        detail::build_payload(frame_format, arbitration_id, xl_header, rx_data.data(), rx_data.size(), payload);
    std::vector<uint8_t> body(payload.begin(), payload.begin() + static_cast<long>(payload_len));

    if (timed) {
        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = false; // read
        hdr.rsp             = true;
        hdr.mtv             = true;
        hdr.transaction_num = transaction_num;
        return acf::encode_acf_gbb(hdr, timestamp, body);
    }
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.rsp             = true;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, body);
}

// Decodes a CAN frame response from either an ACF_ABB or ACF_GBB message
// (peeks the ACF message type itself, unlike decode_frame_request(), since
// a response's encoding depends on the responding endpoint's own
// timed/untimed choice). Fails with the same error set
// decode_frame_request() does. On success, every output parameter is
// populated.
inline std::error_code decode_frame_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              FrameFormat& out_format, uint32_t& out_arbitration_id,
                                              XlHeader& out_xl_header, std::vector<uint8_t>& out_rx_data,
                                              bool& out_timed, uint64_t& out_timestamp,
                                              uint8_t& out_transaction_num) {
    uint8_t msg_type;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(CanErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId        bus_id;
    uint8_t                 evt_op;
    bool                     timed;
    uint64_t                 timestamp;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(CanErrc::short_frame);
        if (ec) return make_error_code(CanErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt_op    = hdr.evt_op;
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(CanErrc::short_frame);
        if (ec) return make_error_code(CanErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt_op    = hdr.evt_op;
        timed     = false;
        timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(CanErrc::wrong_bus);
    if (!acf::evt_row2_is_plain(evt_op)) return make_error_code(CanErrc::bad_evt);

    if (payload.size() < kArbitrationPrefixLen) return make_error_code(CanErrc::short_frame);
    const FrameFormat frame_format = detail::read_frame_format(payload.data());
    if (!frame_format_valid(static_cast<uint8_t>(frame_format))) return make_error_code(CanErrc::bad_frame_format);

    const size_t prefix_len = detail::prefix_len_for(frame_format);
    if (payload.size() < prefix_len) return make_error_code(CanErrc::short_frame);

    detail::read_prefix(payload.data(), frame_format, out_arbitration_id, out_xl_header);
    if (!arbitration_id_valid(frame_format, out_arbitration_id))
        return make_error_code(CanErrc::bad_arbitration_id);

    out_format           = frame_format;
    out_rx_data          = std::vector<uint8_t>(payload.begin() + static_cast<long>(prefix_len), payload.end());
    out_timed             = timed;
    out_timestamp          = timestamp;
    out_transaction_num    = hdr.transaction_num;
    return {};
}

// ── Fragmented response (rcp/fragment.hpp) ────────────────────────────────────
// See this file's own header comment ("Fragmentation wiring") for the full
// picture. Ported from rcp_ep_can_frame_response_fragment_count()/
// _encode_frame_response_fragmented()/_decode_frame_response_fragment()/
// _decode_reassembled_frame_response().

inline size_t frame_response_fragment_count(FrameFormat frame_format, uint32_t arbitration_id,
                                             const std::optional<XlHeader>& xl_header, size_t rx_len,
                                             size_t max_fragment_payload) noexcept {
    if (!detail::encode_preconditions_ok(frame_format, arbitration_id, xl_header, rx_len)) return 0;
    const size_t combined_len = detail::prefix_len_for(frame_format) + rx_len;
    const size_t count        = fragment::plan_count(combined_len, max_fragment_payload);
    if (count > kMaxFragmentSegments) return 0;
    return count;
}

// Encodes a CAN frame response as one or more ACF frames, fragmenting via
// rcp::fragment's ms/segment_num mechanism whenever the combined
// prefix-then-data payload exceeds max_fragment_payload octets. Every
// fragment shares byte_bus_id/evt(0)/op(read)/transaction_num/timed/
// timestamp — frame_format itself lives inside the combined payload's own
// leading quadlet, not per-fragment header state. When the combined payload
// already fits in one fragment, this produces exactly one frame identical
// to what encode_frame_response() itself would have produced. Returns an
// empty vector under the same conditions frame_response_fragment_count()
// returns 0 for. Does not itself apply any E2E CRC — see
// encode_frame_request_fragmented()'s own doc comment.
inline std::vector<std::vector<uint8_t>>
encode_frame_response_fragmented(avtp::ByteBusId byte_bus_id, FrameFormat frame_format,
                                  uint32_t arbitration_id, const std::optional<XlHeader>& xl_header,
                                  const std::vector<uint8_t>& rx_data, uint8_t transaction_num,
                                  bool timed, uint64_t timestamp, size_t max_fragment_payload) {
    const size_t count = frame_response_fragment_count(frame_format, arbitration_id, xl_header,
                                                         rx_data.size(), max_fragment_payload);
    if (count == 0) return {};

    std::array<uint8_t, kXlMaxEncodedLen> combined{};
    const size_t combined_len =
        detail::build_payload(frame_format, arbitration_id, xl_header, rx_data.data(), rx_data.size(), combined);

    std::array<fragment::Segment, kMaxFragmentSegments> segs{};
    if (fragment::plan(combined_len, max_fragment_payload, segs.data(), count)) return {};

    std::vector<std::vector<uint8_t>> out_frames;
    out_frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::vector<uint8_t> slice(combined.begin() + static_cast<long>(segs[i].offset),
                                    combined.begin() + static_cast<long>(segs[i].offset + segs[i].len));

        if (timed) {
            acf::AcfMessageInfo hdr;
            hdr.byte_bus_id              = byte_bus_id;
            hdr.op                       = false; // read
            hdr.rsp                      = true;
            hdr.mtv                      = true;
            hdr.transaction_num          = transaction_num;
            hdr.ms                       = segs[i].ms;
            hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0;
            out_frames.push_back(acf::encode_acf_gbb(hdr, timestamp, slice));
        } else {
            acf::AcfMessageInfo hdr;
            hdr.byte_bus_id              = byte_bus_id;
            hdr.op                       = false; // read
            hdr.rsp                      = true;
            hdr.transaction_num          = transaction_num;
            hdr.ms                       = segs[i].ms;
            hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0;
            out_frames.push_back(acf::encode_acf_abb(hdr, slice));
        }
    }
    return out_frames;
}

// Decodes one fragment of a (possibly multi-fragment) CAN frame response
// from b[0..len) — the same peek-message-type/byte_bus_id validation
// decode_frame_response() applies, but this function does NOT strip Figure
// 39's leading-quadlet-then-data layout from the payload (a fragment other
// than the first may not even contain the whole leading quadlet —
// fragmentation operates on the flat combined byte sequence, agnostic to
// its own internal structure). Since frame_format lives inside that
// leading quadlet rather than in evt, it is not obtainable per-fragment at
// all — decode_reassembled_frame_response() recovers it once, after
// reassembly. This function instead surfaces the fragment's own ms bit,
// read_size_or_segment_num (as *out_segment_num, meaningful only when
// *out_ms), and raw ACF payload, for a caller to feed straight into a
// rcp::fragment::Reassembler.
inline std::error_code decode_frame_response_fragment(const uint8_t* b, size_t len,
                                                        avtp::ByteBusId expected_bus_id, bool& out_ms,
                                                        uint16_t& out_segment_num,
                                                        std::vector<uint8_t>& out_payload, bool& out_timed,
                                                        uint64_t& out_timestamp,
                                                        uint8_t& out_transaction_num) {
    uint8_t msg_type;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(CanErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId        bus_id;
    uint8_t                 evt_op;
    bool                     timed;
    uint64_t                 timestamp;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(CanErrc::short_frame);
        if (ec) return make_error_code(CanErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt_op    = hdr.evt_op;
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(CanErrc::short_frame);
        if (ec) return make_error_code(CanErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt_op    = hdr.evt_op;
        timed     = false;
        timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(CanErrc::wrong_bus);
    if (!acf::evt_row2_is_plain(evt_op)) return make_error_code(CanErrc::bad_evt);

    out_ms               = hdr.ms;
    out_segment_num       = hdr.read_size_or_segment_num;
    out_payload            = std::move(payload);
    out_timed               = timed;
    out_timestamp            = timestamp;
    out_transaction_num       = hdr.transaction_num;
    return {};
}

// Applies Figure 39's leading-quadlet-then-data parsing to a fully
// reassembled combined payload — rcp::fragment::Reassembler::data()/size()'s
// output once feed() has reported ReasmResult::kComplete. This is the
// second half of what decode_frame_response() does in one step for a
// single, unfragmented frame.
inline std::error_code decode_reassembled_frame_response(const uint8_t* reassembled, size_t reassembled_len,
                                                           FrameFormat& out_format,
                                                           uint32_t& out_arbitration_id,
                                                           XlHeader& out_xl_header,
                                                           std::vector<uint8_t>& out_rx_data) {
    if (reassembled_len < kArbitrationPrefixLen) return make_error_code(CanErrc::short_frame);
    const FrameFormat frame_format = detail::read_frame_format(reassembled);
    if (!frame_format_valid(static_cast<uint8_t>(frame_format))) return make_error_code(CanErrc::bad_frame_format);

    const size_t prefix_len = detail::prefix_len_for(frame_format);
    if (reassembled_len < prefix_len) return make_error_code(CanErrc::short_frame);

    detail::read_prefix(reassembled, frame_format, out_arbitration_id, out_xl_header);
    if (!arbitration_id_valid(frame_format, out_arbitration_id))
        return make_error_code(CanErrc::bad_arbitration_id);

    out_format  = frame_format;
    out_rx_data = std::vector<uint8_t>(reassembled + prefix_len, reassembled + reassembled_len);
    return {};
}

// ── CanEndpoint ───────────────────────────────────────────────────────────────
// No TriggerRegistry member: unlike every other device-facing endpoint type
// in this codebase, the specification defines no trigger-signal table for
// CAN at all (extraction §7) — CanEndpoint deliberately has no
// TriggerRegistry member and no signal-id helper function.
//
// transmit()/receive()/handle_request()/last_transmitted()/last_received()
// below are UNCHANGED from this header's pre-existing content (signature
// and behavior) — rcp/mock.hpp's dispatch_can() already calls
// `can_.handle_request(req.evt_op, frame)` with this exact signature and
// this pass does not touch rcp/mock.hpp.
class CanEndpoint {
public:
    CanFunctionalConfig&       functional_config() noexcept { return cfg_; }
    const CanFunctionalConfig& functional_config() const noexcept { return cfg_; }

    // General (non-XL) acceptance-filter bank — see CanAcceptanceFilter's
    // own comment for why this has no c-RCP equivalent.
    void set_acceptance_filters(std::vector<CanAcceptanceFilter> filters) {
        filters_ = std::move(filters);
    }
    const std::vector<CanAcceptanceFilter>& acceptance_filters() const noexcept { return filters_; }

    std::error_code transmit(CanDataFrame frame) {
        auto ec = validate_frame(frame);
        if (ec) return ec;
        last_tx_ = std::move(frame);
        return {};
    }

    // handle_request is CAN's request-decode entry point, mirroring
    // rcp::i2c::I2cEndpoint::handle_request's shape (this repo's fifth
    // Table 33 Row 2 endpoint type after I2C, ADC, PWM_IN, and LIN). It
    // classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of before doing anything else, so a
    // Reserved value can never reach transmit() and be misread as an
    // ordinary transmit request, and a ConfigWrite value can never be
    // silently accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transmit() with
    //     `frame` unchanged.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching any
    //     endpoint state (last_tx_ is left exactly as it was).
    //   - ConfigWrite (evt[2:0] == 111b): returns
    //     CanErrc::config_write_not_supported — this handle_request/
    //     transmit() object-model path is deliberately independent of the
    //     new apply_reconfig()/CanFunctionalConfig surface added by this
    //     pass, same as rcp/lin.hpp's identical note.
    //
    // NOT to be confused with Figure 39's own "FrameFormat" sub-field,
    // carried inside the request's own payload alongside the arbitration id
    // and CAN data: that field selects the CAN frame's own on-wire format
    // and lives entirely in the payload — see this file's own header
    // comment and encode_frame_request()/decode_frame_request() above.
    std::error_code handle_request(uint8_t evt_op, CanDataFrame frame) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transmit(std::move(frame));
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(CanErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    // receive models one inbound data frame arriving off the bus. Returns
    // false (and does not record the frame) when the frame's own
    // format-scoped filter bank has at least one enabled entry and none of
    // them match — true otherwise, including whenever no filter bank
    // applies at all. A CAN XL frame is matched against
    // functional_config().xl_filters (Table 56's own register-modeled
    // filter table); every other frame is matched against the general
    // acceptance_filters() bank (this implementation's own extension).
    bool receive(CanDataFrame frame) {
        bool matched;
        if (frame_format_is_xl(frame.format)) {
            const bool any_enabled = std::any_of(cfg_.xl_filters.begin(), cfg_.xl_filters.end(),
                                                  [](const CanXlFilter& f) { return f.enable; });
            matched = !any_enabled ||
                      std::any_of(cfg_.xl_filters.begin(), cfg_.xl_filters.end(), [&](const CanXlFilter& f) {
                          return f.enable && (frame.arbitration_id & f.mask) == (f.id & f.mask);
                      });
        } else {
            matched = filters_.empty() ||
                      std::any_of(filters_.begin(), filters_.end(), [&](const CanAcceptanceFilter& f) {
                          return acceptance_filter_matches(f, frame.format, frame.arbitration_id);
                      });
        }
        if (!matched) return false;
        last_rx_ = std::move(frame);
        return true;
    }

    const CanDataFrame& last_transmitted() const noexcept { return last_tx_; }
    const CanDataFrame& last_received() const noexcept { return last_rx_; }

private:
    CanFunctionalConfig               cfg_;
    std::vector<CanAcceptanceFilter>  filters_;
    CanDataFrame                      last_tx_;
    CanDataFrame                      last_rx_;
};

} // namespace can
} // namespace rcp

// Enable std::error_code construction from rcp::can::CanErrc/CanReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::can::CanErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::can::CanReconfigErrc> : true_type {};
} // namespace std
