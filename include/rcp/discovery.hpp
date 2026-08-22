// fusa:req REQ-DISC-001
// fusa:req REQ-DISC-002
// fusa:req REQ-DISC-003
// fusa:req REQ-DISC-004
// fusa:req REQ-DISC-005
// fusa:req REQ-DISC-006
// fusa:req REQ-DISC-007
// fusa:req REQ-DISC-008
// fusa:req REQ-DISC-009
// fusa:req REQ-DISC-010
// fusa:req REQ-DISC-011
// fusa:req REQ-DISC-012
// fusa:req REQ-DISC-013
// fusa:req REQ-DISC-014
// fusa:req REQ-DISC-015
// fusa:req REQ-DISC-016
// fusa:req REQ-DISC-017
// fusa:req REQ-DISC-018
// fusa:req REQ-DISC-019
// fusa:req REQ-DISC-020
// fusa:req REQ-DISC-021
// fusa:req REQ-DISC-022
// fusa:req REQ-DISC-023
// fusa:req REQ-DISC-024
// fusa:req REQ-DISC-025
// fusa:req REQ-DISC-026
// fusa:req REQ-DISC-027
// fusa:req REQ-DISC-028
// fusa:req REQ-DISC-030

// TC18 requirements-corpus completeness pass: REQ-DISC-029 is catalogued in
// this module's own requirements catalog with a "tc18" citation and a
// "status" of "not-implemented" -- it describes normative TC18 behaviour
// (DISCOVERY_STREAM_OCCUPIED, an error label that appears only in TC18
// Figure 17's diagram, not among Table 30's 17 numbered wire error codes)
// this module does NOT itself resolve; see on_discovery_request()'s own
// doc comment. Its test pins the deviation rather than force-mapping it.
// fusa:req REQ-DISC-029

// RC Server discovery -- the broadcastable, byte_bus_id-0 read request every
// OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC server
// must answer regardless of lifecycle state, plus discovery-stream claiming
// for configuration (extraction §3.1, §3.5), the discovery response itself
// (a register-map slice at address 0 populated from the server's own
// generic-recognition fields, extraction §12.7.5 Table 18), its Phase 20
// (rcp/fragment.hpp) fragmented-response counterpart, and a purely
// client-side discovery-result cache.
//
// ROADMAP.md Phase 17 (cpp-RCP issue #129), "Phase 4" (server/dispatch
// layer): re-derived in full from c-RCP's include/rcp/discovery.h +
// src/discovery.c (this project's RC5-spec-conformant reference
// implementation for this module) -- this header now rides on
// rcp/avtp.hpp's NTSCF framing, rcp/acf.hpp's ACF_ABB message format and
// byte_bus_id addressing, rcp/lifecycle.hpp's ServerState (to decide
// whether an incoming discovery request is eligible to claim the discovery
// stream), rcp/regmap.hpp's GeneralMap (the register-map slice a discovery
// response actually carries) and kEp0 (byte_bus_id 0 / register-map address
// 0, the general bootstrap/magic-number field region), and rcp/fragment.hpp
// (the generic ms/segment_num primitive a discovery response's own
// Phase-20 fragmented counterpart wires in). This module does not itself
// implement a byte-level serialization of the whole register map to and
// from the wire -- only the leading, generic-recognition slice
// (kDiscoveryGeneralSliceLen below) that a discovery response actually
// carries; rcp/regmap.hpp's own render()/encode_read_response() own the
// full Table 20 extent as a separate, later-milestone concern.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete claim-state
// machine and default timeout chosen in this file are this implementation's
// own encoding of that behavior, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/acf.hpp, rcp/lifecycle.hpp, rcp/regmap.hpp, and
// rcp/fragment.hpp.
//
// ── The discovery request/response exchange ─────────────────────────────────
//
// Discovery is a single ACF_ABB read request addressed to kDiscoveryByteBusId
// -- the same id regmap::kEp0 uses -- and is the one request an RC Server
// answers regardless of its current lifecycle::ServerState.
// encode_discovery_request()/decode_discovery_request() build and parse
// that request; encode_discovery_response()/decode_discovery_response()
// build and parse the reply, a register-map slice starting at address 0 of
// the requested read_size, populated from regmap::GeneralMap's own
// generic-recognition fields (magic, svr_version, vendor_id, device_id,
// svr_ep_count) -- see kDiscoveryGeneralSliceLen below for exactly how many
// of those octets this milestone actually defines. Anything vendor/device-
// specific beyond that generic slice is explicitly out of this exchange's
// scope (e.g. carried in a datasheet, not on the wire here).
//
// ── NTSCF-only ───────────────────────────────────────────────────────────────
//
// A discovery request or response never rides on a TSCF-headed frame, in
// any lifecycle state, independent of an RC Node's own time-sync
// capability. This is a dedicated, directly-testable rule
// (should_drop_discovery(), mirroring rcp/avtp.hpp's own
// is_tscf_should_drop()-shaped convention) layered on top of -- not a
// replacement for -- rcp/avtp.hpp's general time-sync-gated TSCF drop rule
// and rcp/lifecycle.hpp's own per-state acceptance filtering.
//
// ── Discovery-stream claiming ───────────────────────────────────────────────
//
// DiscoveryClaim models the reservation described in extraction §3.5: the
// first discovery request a server receives while it is in HwUnconfigured
// or HwConfigured reserves the discovery stream for that client's
// subsequent configuration writes, until Discovery_TimeOut elapses with no
// follow-up configuration write (on_configuration_request() extends the
// reservation on each one, matching c-RCP's rcp_discovery_claim_note_config_write()
// — see that method's own doc comment; this port's own prior revision
// instead released the claim on its very first configuration write, a
// genuine behavioral bug versus c-RCP fixed in this pass — see that
// method's own doc comment for the file:line citation). The timeout is a configurable
// constructor parameter (mirroring rcp/regmap.hpp's own request-stream
// rx_wd_timeout_ms convention), defaulted to kDefaultTimeout, not hardcoded
// elsewhere in this module. A lapsed or never-held claim is open and may be
// granted to a new requester; ordinary read-only discovery from any client
// is unaffected by claim state (it is answered per the paragraph above
// regardless), only a configuration *write*'s authorization consults the
// claim. This module deliberately does not itself decide whether a given
// register field is writable in the server's current lifecycle state --
// that remains rcp/lifecycle.hpp's own field-writability logic's job; a
// caller combines may_configure()'s answer with that as one more input to
// an overall write-authorization decision.
//
// Every method below that reasons about elapsed time takes an explicit
// TimePoint parameter (std::chrono::steady_clock::now(), read by the
// caller) rather than reaching for the clock itself -- matching this
// codebase's standing convention (see rcp/avtp.hpp/rcp/acf.hpp/
// rcp/lifecycle.hpp) of functions here consuming already-classified/
// already-read inputs. This also makes the claim/timeout state machine
// fully deterministic to test.
//
// ── Client-side discovery result persistence ────────────────────────────────
//
// DiscoveryCache is an explicitly thin convenience API, not a protocol
// requirement: nothing in this module consults it to decide whether to
// (re-)issue a discovery request, and a client that never touches it
// remains fully conformant. It exists purely so a client on a known,
// stable topology is not forced to rediscover every power cycle, should it
// choose to persist (or simply cache in-process) a prior DiscoveryResult.
//
// ── regmap::ep_id_map::is_valid_association() interplay: none ──────────────
//
// c-RCP's own discovery.c never calls rcp_regmap_ep_id_map_is_valid_association()
// (or any equivalent stream_id/byte_bus_id association check) -- discovery's
// own responsibility begins and ends with the claim/timeout state machine
// above; whether a *non-discovery* stream_id/byte_bus_id pairing is a valid
// association is entirely regmap.hpp's/lifecycle.hpp's own concern (see
// lifecycle.hpp's via_valid_stream_association). This port follows that
// same division of labor: nothing below calls
// regmap::ep_id_map::is_valid_association().
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/regmap.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace discovery {

// ── Discovery addressing ─────────────────────────────────────────────────────
// A discovery request targets byte_bus_id 0 — the same id regmap::kEp0 uses
// — and, within whatever EP0 exposes, register-map address 0: the general
// bootstrap fields, magic number first among them (extraction §3.5).

constexpr avtp::ByteBusId kDiscoveryByteBusId = static_cast<avtp::ByteBusId>(regmap::kEp0);
constexpr uint32_t        kDiscoveryRegisterAddress = 0;

// A discovery read's default response size: just enough to carry the 32-bit
// magic number at register-map address 0. Callers reading more of the
// bootstrap block may pass a larger read_size to make_discovery_request.
constexpr uint16_t kDiscoveryDefaultReadSize = sizeof(uint32_t);

// The number of leading octets of the general register slice this
// milestone actually populates: magic (4) + svr_version (4) + vendor_id
// (2) + device_id (2) + svr_ep_count (2), each big-endian, in that order --
// the field widths and their order are the specification's own for the
// leading, device-recognition part of the RC Server general register map
// (TC18 v0.5.1_RC §12.7.5 Table 18 "RC Server configuration static part",
// absolute addresses 0x0000..0x000D); only the field set regmap::GeneralMap
// already models is drawn from it, by reference. svr_version is FOUR octets
// wide, not two: a two-octet svr_version shifts vendor_id, device_id and
// svr_ep_count each two octets earlier than a conforming peer reads them,
// so every one of those three fields is misparsed -- pinned exactly by
// this file's own tests (mirroring c-RCP's own regression test for this,
// tests/test_discovery.c's test_response_general_slice_octet_layout()). A
// response's payload length always equals the request's read_size exactly:
// the leading min(read_size, kDiscoveryGeneralSliceLen) octets carry this
// slice, and any remaining octets (a read_size greater than this constant)
// are zero-filled reserved space for fields a future milestone may define.
constexpr size_t kDiscoveryGeneralSliceLen = 14;

// ── Errors ────────────────────────────────────────────────────────────────────
// Numbered to match c-RCP's own rcp_discovery_errc_t (include/rcp/discovery.h)
// 1:1 for cross-reference, though the numeric value itself carries no
// meaning to std::error_code beyond equality within this category.
enum class DiscoveryErrc : int {
    short_frame = 1, // b is shorter than either the AVTP or ACF fixed header, or than their declared payload lengths
    // A TSCF-headed (or otherwise unrecognized-subtype) discovery frame was
    // received. Discovery is NTSCF-only (extraction §3.5); such a frame is
    // dropped rather than answered or partially processed. Checked before
    // any ACF-level parsing is attempted (see should_drop_discovery()).
    tscf_headed_request_dropped = 2,
    bad_msg_type                = 3, // the NTSCF payload is not an ACF_ABB message
    wrong_bus                   = 4, // the ACF header's byte_bus_id is not kDiscoveryByteBusId
    wrong_op                    = 5, // the ACF header's op is not "read"
};

inline const std::error_category& discovery_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.discovery"; }
        std::string message(int ev) const override {
            switch (static_cast<DiscoveryErrc>(ev)) {
            case DiscoveryErrc::short_frame:
                return "rcp/discovery: frame too short";
            case DiscoveryErrc::tscf_headed_request_dropped:
                return "rcp/discovery: TSCF-headed discovery request dropped (discovery is NTSCF-only)";
            case DiscoveryErrc::bad_msg_type:
                return "rcp/discovery: unexpected ACF message type";
            case DiscoveryErrc::wrong_bus:
                return "rcp/discovery: wrong byte_bus_id";
            case DiscoveryErrc::wrong_op:
                return "rcp/discovery: wrong ACF op";
            default:
                return "rcp/discovery: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(DiscoveryErrc e) noexcept {
    return {static_cast<int>(e), discovery_category()};
}

// ── NTSCF-only rule ────────────────────────────────────────────────────────── (REQ-DISC-001)

// True iff avtp_subtype is anything other than avtp::kSubtypeNtscf — in
// particular, true for avtp::kSubtypeTscf regardless of lifecycle state or
// time-sync capability, and true for any other, unrecognized subtype byte.
// Ported from c-RCP's rcp_discovery_should_drop() (src/discovery.c).
constexpr bool should_drop_discovery(uint8_t avtp_subtype) noexcept {
    return avtp_subtype != avtp::kSubtypeNtscf;
}

// ── Shared request/response ACF-level validation ──────────────────────────── (REQ-DISC-002..008/012..014)

namespace detail {

// decode_common_frame — common to both decode_discovery_request() and
// decode_discovery_response()/decode_discovery_response_fragment(): peel
// the NTSCF layer (applying the NTSCF-only rule first, REQ-DISC-001), then
// the ACF_ABB layer, then check byte_bus_id and op. Neither caller is
// expected to see a non-ABB, wrong-bus, or wrong-op frame in practice, but
// both validate the same way rather than assuming it. Ported from c-RCP's
// decode_common() (src/discovery.c:68-102).
inline std::error_code decode_common_frame(const uint8_t* buf, size_t len,
                                            avtp::NtscfHeader& out_hdr,
                                            acf::AcfMessageInfo& out_info,
                                            std::vector<uint8_t>& out_payload) {
    if (len < 1) return make_error_code(DiscoveryErrc::short_frame);

    // Checked before any further parsing is attempted, per this file's own
    // header comment: a TSCF-headed (or otherwise non-NTSCF) frame is
    // dropped outright, independent of lifecycle state or time-sync
    // capability.
    if (should_drop_discovery(buf[0])) return make_error_code(DiscoveryErrc::tscf_headed_request_dropped);

    const auto ntscf_ec = avtp::decode_ntscf_header(buf, len, out_hdr);
    if (ntscf_ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer))
        return make_error_code(DiscoveryErrc::short_frame);
    if (ntscf_ec) return make_error_code(DiscoveryErrc::tscf_headed_request_dropped);

    const size_t acf_off = avtp::kNtscfHeaderLen;
    if (len < acf_off) return make_error_code(DiscoveryErrc::short_frame);

    const auto acf_ec = acf::decode_acf_abb(buf + acf_off, len - acf_off, out_info, out_payload);
    if (acf_ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer))
        return make_error_code(DiscoveryErrc::short_frame);
    if (acf_ec) return make_error_code(DiscoveryErrc::bad_msg_type);

    if (out_info.byte_bus_id != kDiscoveryByteBusId) return make_error_code(DiscoveryErrc::wrong_bus);
    if (out_info.op) return make_error_code(DiscoveryErrc::wrong_op); // discovery is always a read

    return {};
}

} // namespace detail

// ── Discovery request framing ────────────────────────────────────────────────
// make_discovery_request builds the ACF_ABB-level header for a discovery
// read: unconditional read (op=false), targeting byte_bus_id 0
// (extraction §3.5). It rides on acf::make_standard_request unchanged —
// discovery is the mandatory standard request kind addressed at a fixed
// endpoint, not a distinct wire-level message shape of its own.
inline acf::AcfMessageInfo make_discovery_request(uint8_t transaction_num,
                                                    uint16_t read_size = kDiscoveryDefaultReadSize) noexcept {
    return acf::make_standard_request(kDiscoveryByteBusId, transaction_num,
                                       /*write=*/false, read_size);
}

// encode_discovery_request wraps make_discovery_request's ACF_ABB message in
// an NTSCF header addressed to `stream_id`. Discovery requests are
// NTSCF-only and broadcastable (extraction §3.5) — this function never
// produces a TSCF-headed frame, so there is no way to misuse it into
// building the kind of request decode_discovery_request below must drop.
inline std::vector<uint8_t> encode_discovery_request(const avtp::StreamId& stream_id,
                                                       uint8_t sequence_num,
                                                       uint8_t transaction_num,
                                                       uint16_t read_size = kDiscoveryDefaultReadSize) {
    const auto info = make_discovery_request(transaction_num, read_size);
    const auto acf_msg = acf::encode_acf_abb(info, {});

    avtp::NtscfHeader hdr;
    hdr.stream_id           = stream_id;
    hdr.sequence_num        = sequence_num;
    hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = avtp::encode_ntscf_header(hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

// decode_discovery_request decodes a raw AVTPDU frame as a discovery
// request via the shared detail::decode_common_frame() validation above:
// NTSCF-only (DiscoveryErrc::tscf_headed_request_dropped otherwise),
// ACF_ABB (DiscoveryErrc::bad_msg_type otherwise), addressed to
// kDiscoveryByteBusId (DiscoveryErrc::wrong_bus otherwise), and a read, not
// a write (DiscoveryErrc::wrong_op otherwise) — DiscoveryErrc::short_frame
// for any AVTP/ACF-level short buffer along the way. On success, out_hdr's
// own stream_id is the requester's identity (NTSCF's stream_id is always
// sender-assigned) and out_info.read_size_or_segment_num/transaction_num
// are the request's own read_size and transaction_num.
inline std::error_code decode_discovery_request(const uint8_t* buf, size_t len,
                                                  avtp::NtscfHeader& out_hdr,
                                                  acf::AcfMessageInfo& out_info,
                                                  std::vector<uint8_t>& out_payload) {
    return detail::decode_common_frame(buf, len, out_hdr, out_info, out_payload);
}

// should_answer_discovery documents, as a single always-true call site
// rather than an implicit assumption scattered across a lifecycle switch
// statement, that a server answers a (NTSCF-headed) discovery request in
// *every* lifecycle state — including RcpConfigured, where the claiming
// mechanism below no longer applies but discovery reads still must
// (extraction §3.1, §3.5's mandatory-baseline requirement).
constexpr bool should_answer_discovery(lifecycle::ServerState /*state*/) noexcept {
    return true;
}

// ── Discovery response ─────────────────────────────────────────────────────── (REQ-DISC-009..014)

namespace detail {

// build_general_slice_payload — builds a read_size-octet payload: the
// leading min(read_size, kDiscoveryGeneralSliceLen) octets carry map's own
// magic/svr_version/vendor_id/device_id/svr_ep_count, each big-endian, in
// that order (see kDiscoveryGeneralSliceLen's own doc comment); any
// remaining octets are left zero-filled reserved space. Shared by
// encode_discovery_response() and encode_discovery_response_fragmented()
// below — c-RCP's own rcp_discovery_encode_response()/
// _encode_response_fragmented() (src/discovery.c) duplicate this same
// slice-building logic inline in both functions; folded into one helper
// here instead.
inline std::vector<uint8_t> build_general_slice_payload(const regmap::GeneralMap& map,
                                                          uint8_t read_size) {
    std::array<uint8_t, kDiscoveryGeneralSliceLen> slice{};
    avtp::detail::put_u32(&slice[0], map.magic);
    avtp::detail::put_u32(&slice[4], map.svr_version); // 32 bit, not 16 -- see kDiscoveryGeneralSliceLen
    avtp::detail::put_u16(&slice[8], map.vendor_id);
    avtp::detail::put_u16(&slice[10], map.device_id);
    avtp::detail::put_u16(&slice[12], map.svr_ep_count);

    // A response's payload always spans exactly read_size octets -- see
    // kDiscoveryGeneralSliceLen's own comment. Any octets beyond the
    // populated slice are left as this zero-fill.
    std::vector<uint8_t> payload(read_size, 0);
    const size_t copy_len = std::min<size_t>(read_size, kDiscoveryGeneralSliceLen);
    std::copy(slice.begin(), slice.begin() + static_cast<std::ptrdiff_t>(copy_len), payload.begin());
    return payload;
}

} // namespace detail

// make_discovery_response builds the ACF_ABB-level header for a discovery
// response: a read (op=false) response (rsp=true — TC18.txt:1885, rsp=1b
// identifies a response), echoing read_size and transaction_num.
inline acf::AcfMessageInfo make_discovery_response(uint8_t read_size, uint8_t transaction_num) noexcept {
    acf::AcfMessageInfo info;
    info.byte_bus_id              = kDiscoveryByteBusId;
    info.op                        = false; // read
    info.rsp                       = true;
    info.read_size_or_segment_num = read_size;
    info.transaction_num           = transaction_num;
    return info;
}

// Encodes the discovery response: a full NTSCF-framed ACF_ABB message whose
// payload is exactly read_size octets long (see kDiscoveryGeneralSliceLen
// above), echoing transaction_num from the originating request.
// server_stream_id becomes the NTSCF header's own stream_id — the
// responding RC Server's identity. Addressing the underlying carrier frame
// back to the *requester's* MAC (e.g. as an Ethernet destination address)
// is a transport-level concern this module does not model: a caller wiring
// this to a real carrier uses the requester field decode_discovery_request()
// already surfaces (its NtscfHeader::stream_id.mac) as that destination.
inline std::vector<uint8_t> encode_discovery_response(const regmap::GeneralMap& map,
                                                        const avtp::StreamId& server_stream_id,
                                                        uint8_t sequence_num,
                                                        uint8_t transaction_num,
                                                        uint8_t read_size = static_cast<uint8_t>(kDiscoveryGeneralSliceLen)) {
    const auto info    = make_discovery_response(read_size, transaction_num);
    const auto payload  = detail::build_general_slice_payload(map, read_size);
    const auto acf_msg  = acf::encode_acf_abb(info, payload);

    avtp::NtscfHeader hdr;
    hdr.stream_id           = server_stream_id;
    hdr.sequence_num        = sequence_num;
    hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = avtp::encode_ntscf_header(hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

// A decoded, client-side discovery result: the responding server's own
// identity (from the response frame's NTSCF stream_id) plus the generic
// compatible-device-recognition fields carried in the response's leading
// kDiscoveryGeneralSliceLen octets. valid is always true when this struct
// is populated by decode_discovery_response()/decode_discovery_reassembled_response()
// on success; it exists so DiscoveryCache::find() can hand back a pointer
// to a "no result" cache slot without that ever being confused with a
// genuine all-zero discovery response.
struct DiscoveryResult {
    bool             valid = false;
    avtp::StreamId   server_stream_id{};
    uint32_t         magic = 0;
    uint32_t         svr_version = 0; // 32 bit on the wire -- see kDiscoveryGeneralSliceLen
    uint16_t         vendor_id = 0;
    uint16_t         device_id = 0;
    uint16_t         svr_ep_count = 0;
};

// Decodes a discovery response frame. On success (empty std::error_code),
// out_result is populated with valid = true. Failure modes mirror
// decode_discovery_request()'s AVTP/ACF-level ones (short_frame,
// tscf_headed_request_dropped, bad_msg_type, wrong_bus, wrong_op — a
// response is expected to echo the same byte_bus_id and a read-classified
// op, checked the same way as for a request); a response payload shorter
// than kDiscoveryGeneralSliceLen also yields DiscoveryErrc::short_frame,
// since a genuinely useful result cannot be extracted from it.
inline std::error_code decode_discovery_response(const uint8_t* buf, size_t len,
                                                   DiscoveryResult& out_result) {
    avtp::NtscfHeader     ntscf_hdr;
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>  payload;

    const auto ec = detail::decode_common_frame(buf, len, ntscf_hdr, info, payload);
    if (ec) return ec;

    if (payload.size() < kDiscoveryGeneralSliceLen) return make_error_code(DiscoveryErrc::short_frame);

    out_result.valid            = true;
    out_result.server_stream_id = ntscf_hdr.stream_id;
    out_result.magic            = avtp::detail::get_u32(&payload[0]);
    out_result.svr_version      = avtp::detail::get_u32(&payload[4]);
    out_result.vendor_id        = avtp::detail::get_u16(&payload[8]);
    out_result.device_id        = avtp::detail::get_u16(&payload[10]);
    out_result.svr_ep_count     = avtp::detail::get_u16(&payload[12]);
    return {};
}

// ── Fragmented response (Phase 20, rcp/fragment.hpp) ────────────────────────── (REQ-DISC-025..028)
//
// read_size is one octet wide, so a discovery response's payload (always
// exactly read_size octets, per kDiscoveryGeneralSliceLen's own comment) is
// always well under any plausible max_fragment_payload — this endpoint's
// traffic never actually needs fragment.hpp's ms/segment_num mechanism in
// real-world use, the same reasoning rcp/uart.hpp's own fragmented-read-
// response comment gives for its own endpoint. The functions below are
// nonetheless provided for API consistency across every Phase 20 target
// endpoint and are exercised end-to-end in this module's own test suite
// against a deliberately small max_fragment_payload.
//
// Unlike c-RCP's own rcp_discovery_encode_response_fragmented() (which,
// per an ASIL-D-oriented no-dynamic-allocation push, plans into a fixed
// RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS-capacity (255) stack array rather
// than a heap-allocated one), this port follows this codebase's own
// already-established Phase 20 wiring convention instead (see
// rcp/uart.hpp's encode_read_response_fragmented()): a std::vector<Segment>
// sized to the exact plan count, and a std::vector<std::vector<uint8_t>> of
// encoded frames returned by value. This codebase already allocates freely
// on every other encode path in this file (encode_acf_abb/
// encode_ntscf_header both return std::vector<uint8_t>) — c-RCP's 255-entry
// fixed-array optimization is specific to its own from-scratch
// no-allocation-anywhere goal for this one function and does not carry
// over to a codebase that has not adopted that goal for one-shot wire
// encoders in general (only for persistent state structs, e.g.
// DiscoveryClaim/rcp::fragment::Reassembler's own fixed-capacity buffer).

// The number of ACF frames encode_discovery_response_fragmented() would
// produce for read_size octets of discovery-response payload split into
// fragments of at most max_fragment_payload octets each.
inline size_t discovery_response_fragment_count(uint8_t read_size, size_t max_fragment_payload) noexcept {
    return fragment::plan_count(read_size, max_fragment_payload);
}

// Encodes the discovery response as one or more full NTSCF-framed ACF_ABB
// messages, fragmenting via rcp/fragment.hpp's ms/segment_num mechanism
// whenever read_size exceeds max_fragment_payload octets. Same conventions
// as encode_discovery_response() otherwise (every fragment echoes
// transaction_num and is addressed via server_stream_id); only the ms flag,
// read_size_or_segment_num (meaningful only on an ms=true fragment — the
// final fragment carries read_size itself, exactly as
// encode_discovery_response() always does), and each fragment's own
// payload slice differ. When read_size already fits in one fragment, this
// produces exactly one frame identical to what encode_discovery_response()
// itself would have. Returns an empty vector under the same conditions
// discovery_response_fragment_count() returns 0 for (max_fragment_payload
// == 0 with an oversized read_size, or a split needing more intermediate
// segments than fragment::kMaxIntermediateSegments addresses).
inline std::vector<std::vector<uint8_t>>
encode_discovery_response_fragmented(const regmap::GeneralMap& map,
                                       const avtp::StreamId& server_stream_id,
                                       uint8_t sequence_num,
                                       uint8_t transaction_num,
                                       uint8_t read_size,
                                       size_t max_fragment_payload) {
    const size_t count = discovery_response_fragment_count(read_size, max_fragment_payload);
    if (count == 0) return {};

    std::vector<fragment::Segment> segs(count);
    if (fragment::plan(read_size, max_fragment_payload, segs.data(), count)) return {};

    const auto payload = detail::build_general_slice_payload(map, read_size);

    std::vector<std::vector<uint8_t>> out_frames;
    out_frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::vector<uint8_t> slice(payload.begin() + static_cast<std::ptrdiff_t>(segs[i].offset),
                                    payload.begin() + static_cast<std::ptrdiff_t>(segs[i].offset + segs[i].len));

        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id              = kDiscoveryByteBusId;
        hdr.op                        = false; // read
        hdr.rsp                       = true;  // TC18.txt:1885 -- rsp=1b identifies a response
        hdr.transaction_num           = transaction_num;
        hdr.ms                         = segs[i].ms;
        // The final (ms=false) fragment carries read_size itself -- exactly
        // what encode_discovery_response() above always sends -- not 0;
        // only an ms=true fragment's read_size_or_segment_num means
        // "segment_num" (mirrors c-RCP's src/discovery.c:312).
        hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : static_cast<uint16_t>(read_size);

        const auto acf_msg = acf::encode_acf_abb(hdr, slice);

        avtp::NtscfHeader ntscf_hdr;
        ntscf_hdr.stream_id           = server_stream_id;
        ntscf_hdr.sequence_num        = sequence_num;
        ntscf_hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

        auto frame = avtp::encode_ntscf_header(ntscf_hdr);
        frame.insert(frame.end(), acf_msg.begin(), acf_msg.end());
        out_frames.push_back(std::move(frame));
    }
    return out_frames;
}

// Decodes one fragment of a (possibly multi-fragment) discovery response
// frame — the same AVTP/ACF-level validation decode_discovery_response()
// applies (see that function's own failure-mode list), but surfaces the
// fragment's own ms bit and read_size_or_segment_num (as *out_segment_num,
// meaningful only when *out_ms) alongside the raw ACF payload
// (out_payload), for a caller to feed straight into a
// rcp::fragment::Reassembler. *out_server_stream_id is populated the same
// way decode_discovery_response()'s own result.server_stream_id is.
inline std::error_code decode_discovery_response_fragment(const uint8_t* buf, size_t len,
                                                             avtp::StreamId& out_server_stream_id,
                                                             bool& out_ms, uint8_t& out_segment_num,
                                                             std::vector<uint8_t>& out_payload) {
    avtp::NtscfHeader   ntscf_hdr;
    acf::AcfMessageInfo info;

    const auto ec = detail::decode_common_frame(buf, len, ntscf_hdr, info, out_payload);
    if (ec) return ec;

    out_server_stream_id = ntscf_hdr.stream_id;
    out_ms                 = info.ms;
    out_segment_num        = static_cast<uint8_t>(info.read_size_or_segment_num);
    return {};
}

// Once a rcp::fragment::Reassembler reports ReasmResult::kComplete, applies
// this module's own general-register-slice parsing (see
// kDiscoveryGeneralSliceLen's own comment) to its data()/size() output —
// the second half of what decode_discovery_response() does in one step for
// a single, unfragmented frame. server_stream_id is whatever
// decode_discovery_response_fragment() reported for (any of) that
// sequence's own fragments (round-tripped identically on every fragment of
// one logical response, the same NTSCF sender-assigns-stream_id convention
// every fragment shares). Returns DiscoveryErrc::short_frame if
// reassembled_len is shorter than kDiscoveryGeneralSliceLen, same as
// decode_discovery_response() does for an unfragmented response. On
// success, out_result is populated with valid = true.
inline std::error_code decode_discovery_reassembled_response(const uint8_t* reassembled, size_t reassembled_len,
                                                                const avtp::StreamId& server_stream_id,
                                                                DiscoveryResult& out_result) {
    if (reassembled_len < kDiscoveryGeneralSliceLen) return make_error_code(DiscoveryErrc::short_frame);

    out_result.valid            = true;
    out_result.server_stream_id = server_stream_id;
    out_result.magic            = avtp::detail::get_u32(&reassembled[0]);
    out_result.svr_version      = avtp::detail::get_u32(&reassembled[4]);
    out_result.vendor_id        = avtp::detail::get_u16(&reassembled[8]);
    out_result.device_id        = avtp::detail::get_u16(&reassembled[10]);
    out_result.svr_ep_count     = avtp::detail::get_u16(&reassembled[12]);
    return {};
}

// ── Discovery-stream claiming ──────────────────────────────────────────────── (REQ-DISC-015..022/029)
// DiscoveryClaim models the reservation described in extraction §3.5: the
// first discovery request a server receives while it is in HW_UNCONFIGURED
// or HW_CONFIGURED reserves the discovery stream for that client's
// subsequent configuration writes. If no configuration request follows
// within Discovery_TimeOut, the reservation lapses and any client's next
// discovery request may claim it anew. A claim never blocks reads — every
// client, holder or not, keeps getting discovery reads answered — it only
// gates whether a *configuration* (write) request is allowed to proceed.
//
// Time is supplied by the caller as a std::chrono::steady_clock::time_point
// rather than read internally, so embedding code (and tests) control the
// clock explicitly instead of this class racing a real timer.
class DiscoveryClaim {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Discovery_TimeOut default, ~20 ms per extraction §3.5 (matches
    // c-RCP's own RCP_DISCOVERY_DEFAULT_TIMEOUT_MS). The specification
    // calls this out as configurable; this is this implementation's chosen
    // default, not a mandated value.
    static constexpr std::chrono::milliseconds kDefaultTimeout{20};

    // Outcome of a discovery request arriving at on_discovery_request.
    enum class ClaimOutcome {
        NotEligible, // server_state is neither HwUnconfigured nor HwConfigured; no claim made
        Claimed,     // no active claim existed (or the prior one had lapsed); `client` now holds it
        AlreadyHeld, // `client` already held the still-active claim; unchanged
        HeldByOther, // a different client's claim is still active; this request did not claim
    };

    explicit DiscoveryClaim(std::chrono::milliseconds timeout = kDefaultTimeout) noexcept
        : timeout_(timeout) {}

    // on_discovery_request should be called for every discovery request a
    // server accepts (i.e. every one decode_discovery_request did not drop),
    // tagged with the requesting client's opaque id, the server's current
    // lifecycle state, and the current time. The claiming mechanism itself
    // only applies in HW_UNCONFIGURED/HW_CONFIGURED (extraction §3.5); in
    // RCP_CONFIGURED this always reports NotEligible without touching any
    // existing claim, since by that point configuration is already done.
    // This call never affects whether the request's *read* gets answered —
    // callers must answer it regardless of the outcome returned here.
    //
    // REQ-DISC-029: when the claim is already held by a not-yet-lapsed
    // claimant, this returns HeldByOther (a different client) or
    // AlreadyHeld (the same one re-requesting) — TC18 Figure 17's own two
    // "Discovery request received" transitions read literally ("& no
    // discovery stream assigned -> assign discovery stream -> send
    // discovery response" versus "& discovery stream assigned -> send
    // error response DISCOVERY_STREAM_OCCUPIED") with no carve-out for
    // requester identity, so this refusal-to-(re-)claim applies uniformly
    // either way (re-requesting does not itself refresh the deadline
    // either way — only an actual configuration write does, via
    // on_configuration_request()). DISCOVERY_STREAM_OCCUPIED is a
    // Figure-16-diagram-only label: TC18 §12.9.6 Table 30's own 17
    // numbered wire error codes do not include it, and unlike
    // LOCKED_CONFIG_ACCESS (which cleanly maps onto a numbered code with a
    // semantically matching name), no numbered code here has an obviously
    // corresponding meaning — flagged as a genuine, unresolved ambiguity,
    // not force-mapped. This method's own ClaimOutcome return is therefore
    // as far as this codebase goes: which numbered wire error code (if
    // any) a caller should send for HeldByOther/AlreadyHeld is not decided
    // here.
    ClaimOutcome on_discovery_request(size_t client, lifecycle::ServerState server_state,
                                       TimePoint now) noexcept {
        if (server_state != lifecycle::ServerState::HwUnconfigured &&
            server_state != lifecycle::ServerState::HwConfigured) {
            return ClaimOutcome::NotEligible;
        }
        if (holder_.has_value() && !lapsed(now)) {
            return (*holder_ == client) ? ClaimOutcome::AlreadyHeld : ClaimOutcome::HeldByOther;
        }
        holder_     = client;
        claimed_at_ = now;
        return ClaimOutcome::Claimed;
    }

    // may_configure reports whether `client` currently holds an active,
    // unlapsed claim — i.e. is presently allowed to issue a configuration
    // (write) request on the discovery stream. It does not itself lapse or
    // consume the claim; it is a pure query.
    bool may_configure(size_t client, TimePoint now) const noexcept {
        return holder_.has_value() && *holder_ == client && !lapsed(now);
    }

    // on_configuration_request should be called when a configuration
    // request arrives on the discovery stream. It returns true iff `client`
    // held an active claim at `now`, in which case the claim's deadline is
    // extended by another Discovery_TimeOut window from `now` (matching
    // c-RCP's rcp_discovery_claim_note_config_write(), src/discovery.c:423-430)
    // — a claimant may issue any number of configuration writes over time,
    // each one resetting the clock, so long as no gap between them (or
    // since the initial claim) ever reaches Discovery_TimeOut. A request
    // from a client that does not hold the active claim returns false and
    // leaves the existing claim (if any, held by someone else, or already
    // lapsed) untouched — this never resurrects a lapsed claim, and the
    // real holder's window is never disturbed by an unrelated client's
    // rejected attempt.
    //
    // Bug fix (this port, Phase 4): a prior revision of this method called
    // holder_.reset() here instead — consuming/releasing the claim after
    // its very first configuration write, forcing a claimant to
    // re-request (and re-win) discovery before every subsequent write. That
    // contradicted c-RCP's own ground-truth behavior and its own test
    // (tests/test_discovery.c's test_claim_config_write_refreshes_deadline_for_claimant(),
    // which asserts the claimant is STILL the claimant at a time past the
    // *original* deadline, precisely because the write refreshed it rather
    // than ending the claim) — fixed here to extend instead of release.
    bool on_configuration_request(size_t client, TimePoint now) noexcept {
        if (!may_configure(client, now)) return false;
        claimed_at_ = now; // extends the deadline to now + timeout_, does not release the claim
        return true;
    }

    // has_active_claim / current_holder are read-only introspection for
    // callers (e.g. admin/observability surfaces) that want to report claim
    // state without being able to mutate it.
    bool has_active_claim(TimePoint now) const noexcept {
        return holder_.has_value() && !lapsed(now);
    }
    std::optional<size_t> current_holder(TimePoint now) const noexcept {
        return has_active_claim(now) ? holder_ : std::nullopt;
    }

    // release unconditionally drops the claim (matching c-RCP's
    // rcp_discovery_claim_release()), e.g. on the server's demotion back to
    // HW_UNCONFIGURED (lifecycle.hpp's own reset path).
    void release() noexcept { holder_.reset(); }

    // set_timeout — Phase 4/Phase 17 batch B (cpp-RCP issue #129), added to
    // close a gap this port's own constructor-only timeout left open:
    // c-RCP's rcp_mock_server_set_discovery_timeout_us() (src/mock.c:
    // 669-687) re-derives srv->discovery_claim.timeout_ms from a NEW
    // svr_discovery_timeout register write at any time, explicitly WITHOUT
    // resetting held/claimant/deadline state ("Does NOT reset
    // srv->discovery_claim's own held/claimant/deadline_ms state -- an
    // in-flight claim's own current deadline is unaffected by a
    // timeout-VALUE change mid-claim; only the window a FUTURE grant
    // computes uses the new value"). Before this method existed, this
    // class's timeout_ was fixed for the object's whole lifetime (set once,
    // at construction) -- there was no way for a caller (mock::Server's own
    // set_discovery_timeout_us(), below) to honor that same "update the
    // window, leave any live claim's own current deadline alone" contract
    // without reconstructing (and thereby wrongly resetting) this whole
    // object. Same reasoning applies here: only timeout_ changes; holder_/
    // claimed_at_ are left exactly as they are.
    void set_timeout(std::chrono::milliseconds timeout) noexcept { timeout_ = timeout; }

private:
    bool lapsed(TimePoint now) const noexcept {
        return !holder_.has_value() || (now - claimed_at_) >= timeout_;
    }

    std::chrono::milliseconds timeout_;
    std::optional<size_t>     holder_;
    TimePoint                 claimed_at_{};
};

// ── Client-side discovery result persistence (thin convenience API) ───────── (REQ-DISC-023/030)
// DiscoveryCache is a growable list of previously discovered results, keyed
// by each result's server_stream_id. See this file's own header comment:
// purely a client-side convenience, never itself consulted by this module
// to decide whether to (re-)issue a discovery request. Backed by an
// ordinary std::vector rather than a fixed-capacity buffer — unlike
// DiscoveryClaim (real server-side protocol state on the dispatch hot
// path), this cache is an optional, unbounded, client-owned convenience
// with no ASIL-D no-allocation requirement of its own, matching c-RCP's
// own rcp_discovery_cache_t (a realloc()-grown array).
class DiscoveryCache {
public:
    // Records result, keyed by result.server_stream_id. An existing entry
    // for the same server_stream_id is overwritten in place
    // (last-discovered-wins for that server; no history is retained).
    // Otherwise a new entry is appended.
    void put(const DiscoveryResult& result) {
        for (auto& entry : entries_) {
            if (entry.server_stream_id == result.server_stream_id) {
                entry = result;
                return;
            }
        }
        entries_.push_back(result);
    }

    // Looks up a previously cached result for stream_id. Returns nullptr if
    // none is on record. The returned pointer is borrowed: it is
    // invalidated by any subsequent put() call on this cache.
    const DiscoveryResult* find(const avtp::StreamId& stream_id) const noexcept {
        for (const auto& entry : entries_) {
            if (entry.server_stream_id == stream_id) return &entry;
        }
        return nullptr;
    }

    // Number of entries currently held in this cache.
    size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<DiscoveryResult> entries_;
};

} // namespace discovery
} // namespace rcp

// Enable std::error_code construction from rcp::discovery::DiscoveryErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::discovery::DiscoveryErrc> : true_type {};
} // namespace std
