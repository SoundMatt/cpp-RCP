// fusa:req REQ-MDIO-001
// fusa:req REQ-MDIO-002
// fusa:req REQ-MDIO-003
// fusa:req REQ-MDIO-004
// fusa:req REQ-MDIO-005
// fusa:req REQ-MDIO-006
// fusa:req REQ-MDIO-007
// fusa:req REQ-MDIO-008
// fusa:req REQ-MDIO-009
// fusa:req REQ-MDIO-010
// fusa:req REQ-MDIO-011
// fusa:req REQ-MDIO-012
// fusa:req REQ-MDIO-013
// fusa:req REQ-MDIO-014
// fusa:req REQ-MDIO-015
// fusa:req REQ-MDIO-016
// fusa:req REQ-MDIO-017
// fusa:req REQ-MDIO-018
// fusa:req REQ-MDIO-019
// fusa:req REQ-MDIO-020
// fusa:req REQ-MDIO-021
// fusa:req REQ-MDIO-022
// fusa:req REQ-MDIO-023
// fusa:req REQ-MDIO-024
// fusa:req REQ-MDIO-025
// fusa:req REQ-MDIO-026
// fusa:req REQ-MDIO-027
// fusa:req REQ-MDIO-028

// MDIO endpoint (ep_type 0x0D) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC5's Clause-22 (MMD)/Clause-45 (MMS)
// register-access codec, its Table 59 functional-configuration register
// block (§13.7.13.2), and the Table 33 Row 2 evt[2:0] plain/reserved/
// config-write classification every endpoint type in that row shares
// (§13.5).
//
// Phase 3 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17"), ported from
// c-RCP's include/rcp/ep_mdio.h + src/ep_mdio.c — this project's RC5-spec-
// conformant reference for this module, itself the single largest endpoint
// source file in c-RCP (1038+1195 lines) and the product of a real fix
// history (c-RCP-AUDIT-06/issue #256 Group I's Table 59 register-block +
// evt[2:0]=111b reconfig work, and the REQ-MDIO-021/022/024 mdio_mode/MMS-
// addressing investigation below).
//
// ── MAJOR CONTENT-DRIFT FIX: this header's own prior "addressing-model fix" ──
// (issue #72, cpp-RCP-03) was itself wrong against c-RCP's actual design, and
// is reverted here. That prior pass reasoned that a 5-bit PHY address plus a
// Clause-22/Clause-45 register-address split had "no basis in the TC18 spec's
// own MDIO section" and replaced MdioRequest's addressing with an opaque
// mdio_mode/mdio_address/mdio_payload triple. In fact c-RCP's ep_mdio.h/.c —
// this project's own RC5-conformant source of truth — keeps the Clause-22/
// Clause-45 split as REAL, load-bearing addressing (rcp_ep_mdio_addr_t: a
// 5-bit prtad, plus either nothing further (Clause-22, a 5-bit regad
// directly) or a 5-bit devad + 16-bit regad (Clause-45)) — independently
// public IEEE 802.3 Clause 22/45 knowledge, not spec prose, per ep_mdio.h's
// own file header — and layers a *separate*, additive mdio_mode leading wire
// octet on top of it (REQ-MDIO-021), plus an entirely distinct MMS (Memory
// Map Selector) addressing family (REQ-MDIO-022/024, rcp_ep_mdio_mms_addr_t)
// that the prior pass never modeled at all. This pass restores the real
// Clause-22/Clause-45 (MMD) addressing model below (MdioAddr) alongside the
// new MMS family (MdioMmsAddr) and the complete ACF-level wire codec c-RCP
// actually implements (read/write request/response encode/decode, for both
// families) — none of which existed anywhere in this header before. Given
// the size mismatch this drift left behind (309 lines here vs. c-RCP's 2233),
// this is the largest single content expansion of any Phase 3 batch so far.
//
// mdio_mode's four values, per Table 60 (REQ-MDIO-021):
//   00b: MMD, single word access   (word_count == 1)
//   01b: MMD, multiple byte access (word_count > 1)
//   10b: MMS, single word access   (word_count == 1)
//   11b: MMS, multiple (double) word access (word_count > 1)
// c-RCP's own REQ-MDIO-021 catalog entry documents that Table 60's own
// printed value list assigns `01b` to BOTH MMD rows and leaves `00b`
// unclaimed — a transcription defect, not evidence of a 3-value field —
// resolved the same way this header's own MdioMode enum already did (below):
// 00b is MMD-single, by elimination the only reading giving the field's own
// natural 00/01/10/11 sequence four distinct meanings.
//
// ── REQ-MDIO-024: the MMS addressing ambiguity and how it was closed ────────
// Neither Figure 43 nor Table 60 gives TC18's own `mdio_address` field a bit
// width or internal layout for MMS mode at all (TC18_spec_defects_report.md
// item 55, still open in the specification itself — this port does not
// resolve item 55, it works around it the same way c-RCP does). c-RCP closed
// this with a documented, user-approved, EXTERNALLY-SOURCED assumption (not
// invented from nothing): the OPEN Alliance 10BASE-T1x MAC-PHY Serial
// Interface Specification, V1.1 — a public OPEN Alliance document, NOT the
// confidential TC18 document — whose own control command header (§7.4.1
// Table 4) shows the real protocol "MMS" terminology is almost certainly
// borrowed from: a 4-bit MMS selector (0-15, its own §9.1 Table 6) followed
// by a 16-bit ADDR field. c-RCP's rcp_ep_mdio_mms_addr_t ASSUMES TC18's own
// mdio_address packs the same two sub-fields in the same order, represented
// on THIS module's own wire as two whole octets (`mms`, then a big-endian
// `addr`) — ported below unchanged as MdioMmsAddr. Per REQ-MDIO-022, MMS0
// and MMS1 use 32-bit data words and every other MMS uses 16-bit — that part
// IS TC18-literal (Table 60 states it directly), not an assumption.
// REQ-MDIO-024 is catalogued PARTIAL, not IMPLEMENTED, in c-RCP's own
// .fusa-reqs.json for exactly this reason: the code path fully exists and is
// fully tested against its own assumed layout, but a peer built against a
// different real TC18 committee resolution of item 55 (a different field
// order, width, or meaning) would not interoperate. This port carries that
// same PARTIAL characterization forward rather than presenting the MMS
// family as settled TC18 conformance.
//
// ── Table 33 Row 2 evt[2:0] validation (post-v2.7.0, EIGHTH and last
// endpoint type in this row, after I2C, ADC, PWM_IN, LIN, CAN, UART, and
// ISELED): MdioEndpoint::handle_request is this header's own wiring of
// rcp::endpoint::evt_row2_kind_of — the shared 3-way evt[2:0] classifier for
// Table 33's {ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED, MDIO} row — into
// MDIO's request handling, following the exact shape every sibling
// *Endpoint::handle_request already established. This is retained
// unchanged from before this pass, source-compatible with rcp/mock.hpp's
// existing MdioEndpoint wiring (dispatch_mdio()) — see the "MdioEndpoint
// convenience wrapper" section below for exactly what is and is not
// reframed.
//
// ── No trigger-signal table (drift fix) ──────────────────────────────────────
// This header previously gave MdioEndpoint an invented TriggerRegistry/
// MdioSignal::TransferComplete pair with no c-RCP basis. c-RCP's ep_mdio.h
// own file header states this explicitly and deliberately: "Like ep_can.h
// ... this module defines *no* trigger enumeration ... This mirrors
// ep_can.h's own documented reflection of a gap in the specification itself
// ... rather than an oversight." rcp/can.hpp's own CanEndpoint already
// established the correct pattern for this exact situation (no
// TriggerRegistry member, no signal-id helper) — MdioEndpoint below follows
// it. Nothing outside this header referenced MdioSignal/mdio_signal_id/
// MdioEndpoint::triggers(), so removing them is source-compatible.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete register-key
// composition and wire-layout choices in this file are this implementation's
// own where noted, ported from c-RCP where c-RCP defines the behavior, same
// as the equivalent disclaimers in rcp/avtp.hpp, rcp/regmap.hpp,
// rcp/endpoint.hpp, rcp/i2c.hpp, rcp/adc.hpp, rcp/pwm.hpp, rcp/lin.hpp,
// rcp/can.hpp, rcp/uart.hpp, and rcp/iseled.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rcp {
namespace mdio {

// ── mdio_mode (Table 60, REQ-MDIO-021) ───────────────────────────────────────
// See the file header for the 00b/01b transcription-defect elimination
// reasoning. Values and bit patterns unchanged from before this pass.
enum class MdioMode : uint8_t {
    MmdSingleWord = 0b00,
    MmdMultiWord  = 0b01,
    MmsSingleWord = 0b10,
    MmsMultiWord  = 0b11,
};

constexpr uint8_t kModeOctetMask = 0x03; // bits[1:0] of the leading mdio_mode wire octet

// mode_for_word_count / mms_mode_for_word_count: which MdioMode value a
// request of word_count words selects, for the MMD family and MMS family
// respectively (REQ-MDIO-021/022) — single (word_count == 1) vs. multiple
// (word_count > 1).
constexpr MdioMode mode_for_word_count(size_t word_count) noexcept {
    return word_count > 1 ? MdioMode::MmdMultiWord : MdioMode::MmdSingleWord;
}
constexpr MdioMode mms_mode_for_word_count(size_t word_count) noexcept {
    return word_count > 1 ? MdioMode::MmsMultiWord : MdioMode::MmsSingleWord;
}

// mode_is_unsupported_mms: true iff mode belongs to the MMS family
// (MmsSingleWord/MmsMultiWord) rather than the MMD family. Name kept for
// continuity with c-RCP's rcp_ep_mdio_mode_is_unsupported_mms() (its own
// comment: kept "for source compatibility" even though MMS is no longer
// actually unsupported by this module as a whole) — it means "route to the
// *_mms_* decoder family instead", not "MMS is rejected outright".
constexpr bool mode_is_unsupported_mms(MdioMode mode) noexcept {
    return mode == MdioMode::MmsSingleWord || mode == MdioMode::MmsMultiWord;
}

// payload_width_bits: the width (bits) a single register word carries for
// `mode`, given whether the addressed MMS device (relevant only for
// MmsMultiWord) is MMS0 or MMS1 (Table 60: MMD is always 16-bit; MMS is
// 16-bit except MMS0/MMS1 multi-word access, which is 32-bit). Used by the
// MdioEndpoint convenience wrapper below, not by the ACF-level codec (which
// always knows its own word width from mms_uses_32bit_words()/the MMD
// family's fixed 16-bit width directly).
constexpr uint8_t payload_width_bits(MdioMode mode, bool mms_is_0_or_1) noexcept {
    if (mode == MdioMode::MmsMultiWord && mms_is_0_or_1) return 32;
    return 16;
}

// ── Clause-22 (MMD legacy) / Clause-45 (MMD extended) addressing ────────────
// rcp_ep_mdio_addr_t, ported: independently public IEEE 802.3 Clause 22/45
// knowledge (5-bit port/PHY address, plus either a 5-bit register address
// directly (Clause-22) or a 5-bit device address + 16-bit register address
// (Clause-45)) — not values TC18 itself defines; see the file header.
enum class MdioClause : uint8_t {
    Clause22 = 0, // legacy: 5-bit prtad + 5-bit regad directly; devad must be 0
    Clause45 = 1, // extended: 5-bit prtad + 5-bit devad + full 16-bit regad
};

constexpr uint8_t  kPrtadMax          = 0x1F;
constexpr uint8_t  kDevadMax          = 0x1F;
constexpr uint16_t kClause22RegadMax  = 0x1F;

struct MdioAddr {
    MdioClause clause = MdioClause::Clause22;
    uint8_t    prtad   = 0; // 5-bit port/PHY address, 0..kPrtadMax
    uint8_t    devad    = 0; // meaningful (0..kDevadMax) only for Clause45; must be 0 for Clause22
    uint16_t   regad     = 0; // 0..kClause22RegadMax for Clause22; full 16-bit range for Clause45
};

// addr_valid: false for prtad above kPrtadMax, for a Clause22 address with a
// nonzero devad or a regad above kClause22RegadMax, for a Clause45 address
// with devad above kDevadMax, and for any other clause value; true
// otherwise (REQ-MDIO-001).
constexpr bool addr_valid(MdioAddr addr) noexcept {
    if (addr.prtad > kPrtadMax) return false;
    switch (addr.clause) {
    case MdioClause::Clause22: return addr.devad == 0 && addr.regad <= kClause22RegadMax;
    case MdioClause::Clause45: return addr.devad <= kDevadMax;
    default:                   return false;
    }
}

// burst_next_regad: the next register address one step into a burst
// starting at regad, for clause's own addressing width — wraps at
// kClause22RegadMax (Clause22) or at 0xFFFF (Clause45); returns regad
// unchanged for any other clause value (REQ-MDIO-002).
constexpr uint16_t burst_next_regad(MdioClause clause, uint16_t regad) noexcept {
    switch (clause) {
    case MdioClause::Clause22: return static_cast<uint16_t>((regad + 1) & kClause22RegadMax);
    case MdioClause::Clause45: return static_cast<uint16_t>(regad + 1); // wraps at 16 bits naturally
    default:                   return regad;
    }
}

// ── MMS (Memory Map Selector) addressing (REQ-MDIO-022/024) ─────────────────
// See the file header's own "REQ-MDIO-024" section for the documented,
// externally-sourced assumption this family rests on.
constexpr uint8_t kMmsMax = 0x0F; // 4-bit MMS selector, 0..15, OA-SPI spec Table 6

struct MdioMmsAddr {
    uint8_t  mms  = 0; // Memory Map Selector, 0..kMmsMax
    uint16_t addr = 0; // register address within the selected memory map
};

// mms_addr_valid: true iff addr.mms <= kMmsMax. addr.addr's full 16-bit
// range is always valid — no MMS-specific narrower range is known.
constexpr bool mms_addr_valid(MdioMmsAddr addr) noexcept { return addr.mms <= kMmsMax; }

// mms_uses_32bit_words: true iff mms is 0 or 1 — REQ-MDIO-022's own
// TC18-literal rule (Table 60): MMS0/MMS1 use 32-bit data fields, every
// other mms (2..15) uses 16-bit. Meaningless (but well-defined: false) for
// mms > kMmsMax — callers should have already validated mms via
// mms_addr_valid() first.
constexpr bool mms_uses_32bit_words(uint8_t mms) noexcept { return mms == 0 || mms == 1; }

// mms_burst_next_addr: the next register address one step into an MMS
// burst starting at addr, at MMS addressing's own full 16-bit width (wraps
// at 0xFFFF) — this module's own design choice, like its MMD counterpart.
constexpr uint16_t mms_burst_next_addr(uint16_t addr) noexcept {
    return static_cast<uint16_t>(addr + 1); // wraps at 16 bits naturally
}

// ── Register-word packing: MMD family, always 16-bit (REQ-MDIO-003..008) ────

inline void word_encode(uint16_t word, uint8_t out[2]) noexcept {
    out[0] = static_cast<uint8_t>((word >> 8) & 0xFF);
    out[1] = static_cast<uint8_t>(word & 0xFF);
}

inline uint16_t word_decode(const uint8_t in[2]) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

constexpr size_t pack_len(size_t word_count) noexcept { return word_count * 2; }

// pack_words: packs words[0..word_count) into a newly built big-endian byte
// vector of pack_len(word_count) octets. Returns an empty vector iff
// word_count == 0.
inline std::vector<uint8_t> pack_words(const uint16_t* words, size_t word_count) {
    if (word_count == 0) return {};
    std::vector<uint8_t> out(pack_len(word_count));
    for (size_t i = 0; i < word_count; ++i) word_encode(words[i], &out[2 * i]);
    return out;
}
inline std::vector<uint8_t> pack_words(const std::vector<uint16_t>& words) {
    return pack_words(words.empty() ? nullptr : words.data(), words.size());
}

// word_count_of: true (with out_word_count set to byte_len / 2) iff
// byte_len is even — every packed word occupies exactly 2 octets, so an odd
// byte_len can never hold a whole number of words.
inline bool word_count_of(size_t byte_len, size_t& out_word_count) noexcept {
    if ((byte_len & size_t{1}) != 0) return false;
    out_word_count = byte_len / 2;
    return true;
}

// unpack_word_at: reads the word_index'th packed word out of data. No
// bounds check of its own — caller is responsible for having already
// established (e.g. via word_count_of()) that word_index selects a whole
// word actually present in data.
inline uint16_t unpack_word_at(const uint8_t* data, size_t word_index) noexcept {
    return word_decode(&data[2 * word_index]);
}

// ── Register-word packing: MMS family, 16- or 32-bit per mms (REQ-MDIO-022) ─
// Every word is represented in memory as a uint32_t regardless of its own
// wire width — one packing family instead of two width-specific ones, since
// the width is always a pure function of the already-known `mms` value.

inline void word32_encode(uint32_t word, uint8_t out[4]) noexcept {
    out[0] = static_cast<uint8_t>((word >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((word >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((word >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(word & 0xFF);
}

inline uint32_t word32_decode(const uint8_t in[4]) noexcept {
    return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]);
}

namespace detail {
constexpr size_t mms_word_width(uint8_t mms) noexcept { return mms_uses_32bit_words(mms) ? size_t{4} : size_t{2}; }
} // namespace detail

inline size_t mms_pack_len(uint8_t mms, size_t word_count) noexcept {
    return word_count * detail::mms_word_width(mms);
}

// mms_pack_words: packs words[0..word_count) into a newly built big-endian
// byte vector of mms_pack_len(mms, word_count) octets, at mms's own word
// width (word32_encode() word by word for a 32-bit mms; the low 16 bits of
// each word via word_encode() otherwise). Returns an empty vector iff
// word_count == 0.
inline std::vector<uint8_t> mms_pack_words(uint8_t mms, const uint32_t* words, size_t word_count) {
    if (word_count == 0) return {};
    const size_t          width = detail::mms_word_width(mms);
    std::vector<uint8_t> out(word_count * width);
    for (size_t i = 0; i < word_count; ++i) {
        if (width == 4) {
            word32_encode(words[i], &out[width * i]);
        } else {
            word_encode(static_cast<uint16_t>(words[i] & 0xFFFF), &out[width * i]);
        }
    }
    return out;
}
inline std::vector<uint8_t> mms_pack_words(uint8_t mms, const std::vector<uint32_t>& words) {
    return mms_pack_words(mms, words.empty() ? nullptr : words.data(), words.size());
}

// mms_word_count_of: true (with out_word_count set) iff byte_len is an exact
// multiple of mms's own word width (4 or 2 octets).
inline bool mms_word_count_of(uint8_t mms, size_t byte_len, size_t& out_word_count) noexcept {
    const size_t width = detail::mms_word_width(mms);
    if (byte_len % width != 0) return false;
    out_word_count = byte_len / width;
    return true;
}

// mms_unpack_word_at: reads the word_index'th packed word out of data at
// mms's own width, zero-extended to uint32_t for a 16-bit mms. No bounds
// check of its own.
inline uint32_t mms_unpack_word_at(uint8_t mms, const uint8_t* data, size_t word_index) noexcept {
    const size_t width = detail::mms_word_width(mms);
    if (width == 4) return word32_decode(&data[width * word_index]);
    return static_cast<uint32_t>(word_decode(&data[width * word_index]));
}

// ── Errors ────────────────────────────────────────────────────────────────────
// payload_exceeds_mode_width/config_write_not_supported keep their existing
// values (1/2) — rcp/mock.hpp compares std::error_code equality against
// both by name. Every other enumerator is new, added by this pass for the
// ACF-level codec below (ported from c-RCP's rcp_ep_mdio_errc_t).
enum class MdioErrc : int {
    // mdio_payload does not fit payload_width_bits(mode, mms_is_0_or_1) —
    // MdioEndpoint convenience-wrapper only, see validate_request() below.
    payload_exceeds_mode_width = 1,
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1) at the MdioEndpoint convenience-wrapper's own
    // handle_request() entry point. Reported explicitly rather than
    // silently accepted or silently ignored, same as every sibling
    // *Endpoint::handle_request's own config_write_not_supported variant —
    // the real mechanism is apply_reconfig()/render_registers() below.
    config_write_not_supported = 2,

    // ── ACF-level codec errors (ported from rcp_ep_mdio_errc_t) ──────────────
    short_frame      = 3,
    bad_msg_type     = 4,
    wrong_bus        = 5,
    wrong_op         = 6,
    bad_addr         = 7, // MMD MdioAddr fails addr_valid()
    bad_word_count   = 8,
    // evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
    // plain (non-configuration) request in MDIO's endpoint-type row —
    // caller shall respond with error code UNSUPPORTED_CMD.
    bad_evt          = 9,
    // The decoded mdio_mode octet belongs to the MMS family
    // (mode_is_unsupported_mms()) but was handed to an MMD decoder — use
    // the *_mms_* decoder family instead. Name kept for source continuity
    // with c-RCP's RCP_EP_MDIO_ERR_UNSUPPORTED_MMS (its own comment: no
    // longer means "MMS is unsupported", just "wrong decoder family").
    unsupported_mms  = 10,
    // The decoded MdioMmsAddr fails mms_addr_valid().
    bad_mms_addr     = 11,
    // The decoded mdio_mode octet belongs to the MMD family but was handed
    // to an *_mms_* decoder — the mirror image of unsupported_mms; use the
    // MMD decoder family instead.
    wrong_mdio_mode  = 12,
};

inline const std::error_category& mdio_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.mdio"; }
        std::string message(int ev) const override {
            switch (static_cast<MdioErrc>(ev)) {
            case MdioErrc::payload_exceeds_mode_width:
                return "rcp/mdio: mdio_payload exceeds the width mdio_mode assigns it";
            case MdioErrc::config_write_not_supported:
                return "rcp/mdio: evt[2:0]=111b configuration-write requests are not supported by "
                       "this convenience wrapper — call apply_reconfig() directly";
            case MdioErrc::short_frame:     return "rcp/mdio: frame too short";
            case MdioErrc::bad_msg_type:    return "rcp/mdio: unexpected ACF message type";
            case MdioErrc::wrong_bus:       return "rcp/mdio: wrong byte_bus_id";
            case MdioErrc::wrong_op:        return "rcp/mdio: wrong ACF op";
            case MdioErrc::bad_addr:        return "rcp/mdio: invalid MDIO address";
            case MdioErrc::bad_word_count:  return "rcp/mdio: invalid register-word count";
            case MdioErrc::bad_evt:         return "rcp/mdio: evt[2:0] is not 0b000";
            case MdioErrc::unsupported_mms:
                return "rcp/mdio: frame uses MMS mode — use the *_mms_* decoder family";
            case MdioErrc::bad_mms_addr:    return "rcp/mdio: invalid MMS address";
            case MdioErrc::wrong_mdio_mode:
                return "rcp/mdio: frame uses MMD mode — use the MMD decoder family";
            default:
                return "rcp/mdio: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(MdioErrc e) noexcept {
    return {static_cast<int>(e), mdio_category()};
}

// ── Functional config (§13.7.13.2 Table 59, REQ-MDIO-009/010/020) ───────────
// TC18 §13.7.13.2 opens with "The MDIO EP does not have any configurable
// parameters" — that describes what a *write* can change, not whether the
// block is *readable*: Table 59 still fixes a real register block every
// endpoint type exposes via evt[2:0]=111b. Consequently there is no
// set_ep_status()-shaped mutator here, matching every other endpoint type's
// register-block fields being unreachable except through apply_reconfig().
struct MdioFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint16_t ep_status = 0; // 0x0004, R/W — "to be defined" by the spec itself; round-tripped as-is
};

inline void functional_cfg_init(MdioFunctionalCfg& cfg) noexcept { cfg = MdioFunctionalCfg{}; }

// functional_cfg_writable is a thin, named wrapper over rcp/lifecycle.hpp's
// field_writable() (FieldKind::FunctionalW) — reuses, never duplicates,
// that function's authorization logic.
inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// ── The EP_func register block (evt[2:0] == 111b, Table 59) ─────────────────
// A genuine address-collision editorial defect (c-RCP-AUDIT-06, the fifth
// this audit found, after PWM/GPIO/I2C/ISELED's own): Table 59 prints
// mdio_ep_status at the same relative address (0x0002) as mdio_ep_enable&clr.
// Unlike every other endpoint type's own table, Table 59 defines NO base_clk
// row at all (consistent with "no configurable parameters" — there is
// genuinely no system clock register here), so the minimal, table-literal
// fix is to place mdio_ep_status at the next unclaimed offset after options:
// 0x0004 — one register width narrower than every other endpoint type's
// common prefix, which reserves 0x0004-0x0005 for a base_clk row Table 59
// never lists.
constexpr uint16_t kRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kRegReserved01   = 0x0001; //  8 bit, R
constexpr uint16_t kRegEpEnableClr  = 0x0002; //  8 bit, R/W
constexpr uint16_t kRegEpOptions    = 0x0003; //  8 bit, R/W
constexpr uint16_t kRegEpStatus     = 0x0004; // 16 bit, R/W

// The block's own length in octets — one past the last assigned offset.
// Note this is narrower than every other endpoint type's own kEpFuncLen:
// there is no base_clk row here.
constexpr size_t kEpFuncLen = 0x0006;

using EpFuncBlock = std::array<uint8_t, kEpFuncLen>;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = 1u << 0;
constexpr uint8_t kEnableClrBitClear  = 1u << 4;
constexpr uint8_t kOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kOptionsBitSuppress = 1u << 7;
} // namespace detail

// render_registers serializes cfg's whole EP_func register block into the
// corrected offsets above — the inverse of apply_reconfig()'s own parse
// step.
inline EpFuncBlock render_registers(const MdioFunctionalCfg& cfg) noexcept {
    EpFuncBlock out{};
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
    out[kRegEpStatus]     = static_cast<uint8_t>(cfg.ep_status >> 8);
    out[kRegEpStatus + 1] = static_cast<uint8_t>(cfg.ep_status & 0xFF);
    return out;
}

namespace detail {
inline void parse_registers(MdioFunctionalCfg& cfg, const EpFuncBlock& in) noexcept {
    const uint8_t enable_clr = in[kRegEpEnableClr];
    const uint8_t options    = in[kRegEpOptions];

    cfg.ep_enable             = (enable_clr & kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kOptionsBitSuppress) != 0;

    cfg.ep_status = static_cast<uint16_t>((static_cast<uint16_t>(in[kRegEpStatus]) << 8) | in[kRegEpStatus + 1]);
}

// True iff the octet at relative offset addr belongs to a read-only
// register of the block — EP_LEN and the reserved octet (no base_clk row
// to skip here, unlike every other endpoint type's own common prefix).
constexpr bool reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kRegEpLen || addr == kRegReserved01;
}
} // namespace detail

// The fixed width (octets) of the relative-start-address prefix every
// configuration request's payload begins with.
constexpr size_t kReconfigAddrLen = 2;

enum class MdioReconfigErrc : int {
    short_payload = 1, // payload carries no address prefix, or no data octet after it
    out_of_range  = 2, // start_address + data length exceeds kEpFuncLen — the whole write is ignored
};

inline const std::error_category& mdio_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.mdio.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<MdioReconfigErrc>(ev)) {
            case MdioReconfigErrc::short_payload:
                return "rcp/mdio: configuration write has no address and data";
            case MdioReconfigErrc::out_of_range:
                return "rcp/mdio: configuration write extends past the EP_func block";
            default: return "rcp/mdio: unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(MdioReconfigErrc e) noexcept {
    return {static_cast<int>(e), mdio_reconfig_category()};
}

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is a 16-bit big-endian relative start address followed by the
// configuration data octets to write from that address onward (§12.7.1).
// Same octet-granularity patch, read-only-offset-skip, and
// out-of-range-ignores-the-whole-write rules as every sibling endpoint
// type's own apply_reconfig().
inline std::error_code apply_reconfig(MdioFunctionalCfg& cfg, const uint8_t* payload,
                                       size_t payload_len) noexcept {
    if (payload_len <= kReconfigAddrLen) return make_error_code(MdioReconfigErrc::short_payload);

    const uint16_t start_address =
        static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8) | payload[1]);
    const size_t data_len = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(MdioReconfigErrc::out_of_range);

    EpFuncBlock block = render_registers(cfg);
    for (size_t i = 0; i < data_len; ++i) {
        const uint16_t addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue;
        block[addr] = payload[kReconfigAddrLen + i];
    }
    detail::parse_registers(cfg, block);
    return {};
}

// encode_reconfig_request encodes an ACF_ABB configuration request
// (evt[2:0] == 111b) addressed to byte_bus_id: payload is start_address
// (16-bit big-endian) followed by data. Returns an empty vector if data is
// empty, or if the encoded payload would exceed acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kReconfigAddrLen + data.size());
    payload[0] = static_cast<uint8_t>(start_address >> 8);
    payload[1] = static_cast<uint8_t>(start_address & 0xFF);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kReconfigAddrLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.evt_op          = 0x7;  // evt[2:0] = 111b, the reconfiguration escape hatch
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── Wire-layout constants (this module's own choice, ported from c-RCP's
// ep_mdio.c) — see the file header's "wire-layout" discussion. ────────────
constexpr size_t kMaxBurstWords = 512;

namespace detail {
constexpr size_t kModeOctetLen                = 1; // mdio_mode
constexpr size_t kAddrPrefixLen               = 5; // clause(1)+prtad(1)+devad(1)+regad(2 BE)
constexpr size_t kReadRequestPayloadLen       = kModeOctetLen + kAddrPrefixLen + 2; // + word_count(2 BE)
constexpr size_t kWriteRequestMinPayloadLen   = kModeOctetLen + kAddrPrefixLen;

constexpr size_t kMmsAddrPrefixLen             = 3; // mms(1)+addr(2 BE)
constexpr size_t kMmsReadRequestPayloadLen     = kModeOctetLen + kMmsAddrPrefixLen + 2; // + word_count(2 BE)
constexpr size_t kMmsWriteRequestMinPayloadLen = kModeOctetLen + kMmsAddrPrefixLen;

inline void put_be16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}
inline uint16_t get_be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline void put_addr_prefix(uint8_t* p, MdioAddr addr) noexcept {
    p[0] = static_cast<uint8_t>(addr.clause);
    p[1] = addr.prtad;
    p[2] = addr.devad;
    put_be16(&p[3], addr.regad);
}
inline MdioAddr get_addr_prefix(const uint8_t* p) noexcept {
    MdioAddr addr;
    addr.clause = static_cast<MdioClause>(p[0]);
    addr.prtad  = p[1];
    addr.devad  = p[2];
    addr.regad  = get_be16(&p[3]);
    return addr;
}

inline void put_mms_addr_prefix(uint8_t* p, MdioMmsAddr addr) noexcept {
    p[0] = addr.mms;
    put_be16(&p[1], addr.addr);
}
inline MdioMmsAddr get_mms_addr_prefix(const uint8_t* p) noexcept {
    MdioMmsAddr addr;
    addr.mms  = p[0];
    addr.addr = get_be16(&p[1]);
    return addr;
}
} // namespace detail

// ── Read request/response (MMD family, REQ-MDIO-012..015/025/026) ───────────

// encode_read_request encodes an ACF_ABB read request addressed to
// byte_bus_id: a leading mdio_mode octet (derived from word_count via
// mode_for_word_count()) followed by addr's own clause/prtad/devad/regad
// fields and word_count. Returns an empty vector if !addr_valid(addr), or
// if word_count is 0 or exceeds kMaxBurstWords.
inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id, MdioAddr addr, size_t word_count,
                                                  uint8_t transaction_num) {
    if (!addr_valid(addr)) return {};
    if (word_count == 0 || word_count > kMaxBurstWords) return {};

    std::vector<uint8_t> payload(detail::kReadRequestPayloadLen);
    payload[0] = static_cast<uint8_t>(mode_for_word_count(word_count));
    detail::put_addr_prefix(&payload[detail::kModeOctetLen], addr);
    detail::put_be16(&payload[detail::kModeOctetLen + detail::kAddrPrefixLen], static_cast<uint16_t>(word_count));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// Decodes and validates an ACF-level MDIO read request from b[0..len).
// Fails with MdioErrc::short_frame/bad_msg_type/wrong_bus/wrong_op/bad_evt/
// unsupported_mms/bad_addr/bad_word_count — see the enumerator comments
// above for the exact condition each maps to.
inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                            MdioAddr& out_addr, size_t& out_word_count,
                                            uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
    if (ec) return make_error_code(MdioErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);
    if (hdr.op) return make_error_code(MdioErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(MdioErrc::bad_evt);
    if (payload.size() < detail::kReadRequestPayloadLen) return make_error_code(MdioErrc::short_frame);

    const auto mode = static_cast<MdioMode>(payload[0] & kModeOctetMask);
    if (mode_is_unsupported_mms(mode)) return make_error_code(MdioErrc::unsupported_mms);

    const MdioAddr addr = detail::get_addr_prefix(&payload[detail::kModeOctetLen]);
    if (!addr_valid(addr)) return make_error_code(MdioErrc::bad_addr);

    const uint16_t word_count = detail::get_be16(&payload[detail::kModeOctetLen + detail::kAddrPrefixLen]);
    if (word_count == 0 || static_cast<size_t>(word_count) > kMaxBurstWords)
        return make_error_code(MdioErrc::bad_word_count);

    out_addr             = addr;
    out_word_count        = word_count;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_read_response encodes a read response carrying
// pack_words(words, word_count) as its payload, echoing transaction_num.
// Encoded as ACF_ABB when timed is false; ACF_GBB (message_timestamp =
// timestamp, mtv valid) when timed is true. word_count may be fewer than
// the originating request's own word_count (a short/partial burst read) or
// 0. Returns an empty vector if word_count exceeds kMaxBurstWords.
inline std::vector<uint8_t> encode_read_response(avtp::ByteBusId byte_bus_id, const uint16_t* words,
                                                    size_t word_count, uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp) {
    if (word_count > kMaxBurstWords) return {};
    const std::vector<uint8_t> payload = pack_words(words, word_count);

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.rsp             = true;
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, payload);
    }
    return acf::encode_acf_abb(hdr, payload);
}
inline std::vector<uint8_t> encode_read_response(avtp::ByteBusId byte_bus_id, const std::vector<uint16_t>& words,
                                                    uint8_t transaction_num, bool timed, uint64_t timestamp) {
    return encode_read_response(byte_bus_id, words.empty() ? nullptr : words.data(), words.size(),
                                 transaction_num, timed, timestamp);
}

// decode_read_response decodes a read response from either an ACF_ABB or
// ACF_GBB message (peeks the message type itself, since a response's
// encoding depends on the responding endpoint's own timed/untimed choice).
// out_words_data holds the packed word bytes — unpack_word_at() reads
// individual words out of it.
inline std::error_code decode_read_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                             std::vector<uint8_t>& out_words_data, size_t& out_word_count,
                                             bool& out_timed, uint64_t& out_timestamp,
                                             uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(MdioErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    bool                  timed     = false;
    uint64_t              timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);

    size_t word_count = 0;
    if (!word_count_of(payload.size(), word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_words_data       = std::move(payload);
    out_word_count       = word_count;
    out_timed            = timed;
    out_timestamp        = timestamp;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── Write request/response (MMD family, REQ-MDIO-016..019/027/028) ──────────

// encode_write_request encodes an ACF_ABB write request addressed to
// byte_bus_id: a leading mdio_mode octet followed by addr's own address
// prefix and pack_words(words, word_count). Returns an empty vector if
// !addr_valid(addr), or if word_count is 0 or exceeds kMaxBurstWords.
inline std::vector<uint8_t> encode_write_request(avtp::ByteBusId byte_bus_id, MdioAddr addr,
                                                    const uint16_t* words, size_t word_count,
                                                    uint8_t transaction_num) {
    if (!addr_valid(addr)) return {};
    if (word_count == 0 || word_count > kMaxBurstWords) return {};

    const std::vector<uint8_t> words_bytes = pack_words(words, word_count);
    std::vector<uint8_t>       payload(detail::kWriteRequestMinPayloadLen + words_bytes.size());
    payload[0] = static_cast<uint8_t>(mode_for_word_count(word_count));
    detail::put_addr_prefix(&payload[detail::kModeOctetLen], addr);
    std::copy(words_bytes.begin(), words_bytes.end(),
              payload.begin() + static_cast<long>(detail::kWriteRequestMinPayloadLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}
inline std::vector<uint8_t> encode_write_request(avtp::ByteBusId byte_bus_id, MdioAddr addr,
                                                    const std::vector<uint16_t>& words, uint8_t transaction_num) {
    return encode_write_request(byte_bus_id, addr, words.empty() ? nullptr : words.data(), words.size(),
                                 transaction_num);
}

// Decodes and validates an ACF-level MDIO write request from b[0..len).
// Fails with MdioErrc::short_frame/bad_msg_type/wrong_bus/wrong_op/bad_evt/
// unsupported_mms/bad_addr/bad_word_count. out_words_data holds the packed
// word bytes following the address prefix — unpack_word_at() reads
// individual words out of it.
inline std::error_code decode_write_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              MdioAddr& out_addr, std::vector<uint8_t>& out_words_data,
                                              size_t& out_word_count, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
    if (ec) return make_error_code(MdioErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);
    if (!hdr.op) return make_error_code(MdioErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(MdioErrc::bad_evt);
    if (payload.size() < detail::kWriteRequestMinPayloadLen) return make_error_code(MdioErrc::short_frame);

    const auto mode = static_cast<MdioMode>(payload[0] & kModeOctetMask);
    if (mode_is_unsupported_mms(mode)) return make_error_code(MdioErrc::unsupported_mms);

    const MdioAddr addr = detail::get_addr_prefix(&payload[detail::kModeOctetLen]);
    if (!addr_valid(addr)) return make_error_code(MdioErrc::bad_addr);

    const size_t words_len = payload.size() - detail::kWriteRequestMinPayloadLen;
    size_t       word_count = 0;
    if (!word_count_of(words_len, word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count == 0 || word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_addr       = addr;
    out_words_data = std::vector<uint8_t>(payload.begin() + static_cast<long>(detail::kWriteRequestMinPayloadLen),
                                           payload.end());
    out_word_count       = word_count;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_write_response encodes a write response carrying
// pack_words(accepted_words, accepted_word_count) as its payload, echoing
// transaction_num — the words this endpoint actually accepted (possibly a
// prefix of the originating request's own words on a partial burst, or 0
// for nothing accepted). Encoded as ACF_ABB when timed is false; ACF_GBB
// when timed is true.
inline std::vector<uint8_t> encode_write_response(avtp::ByteBusId byte_bus_id, const uint16_t* accepted_words,
                                                     size_t accepted_word_count, uint8_t transaction_num,
                                                     bool timed, uint64_t timestamp) {
    if (accepted_word_count > kMaxBurstWords) return {};
    const std::vector<uint8_t> payload = pack_words(accepted_words, accepted_word_count);

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.rsp             = true;
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, payload);
    }
    return acf::encode_acf_abb(hdr, payload);
}
inline std::vector<uint8_t> encode_write_response(avtp::ByteBusId byte_bus_id,
                                                     const std::vector<uint16_t>& accepted_words,
                                                     uint8_t transaction_num, bool timed, uint64_t timestamp) {
    return encode_write_response(byte_bus_id, accepted_words.empty() ? nullptr : accepted_words.data(),
                                  accepted_words.size(), transaction_num, timed, timestamp);
}

// decode_write_response decodes a write response from either an ACF_ABB or
// ACF_GBB message (peeked, same reasoning as decode_read_response()).
inline std::error_code decode_write_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              std::vector<uint8_t>& out_words_data, size_t& out_word_count,
                                              bool& out_timed, uint64_t& out_timestamp,
                                              uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(MdioErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    bool                  timed     = false;
    uint64_t              timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);

    size_t word_count = 0;
    if (!word_count_of(payload.size(), word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_words_data       = std::move(payload);
    out_word_count       = word_count;
    out_timed            = timed;
    out_timestamp        = timestamp;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── MMS read request/response (REQ-MDIO-022/024) ────────────────────────────
// The MMS family's own counterpart to the MMD read family above — see the
// file header's "REQ-MDIO-024" section for the wire layout and its
// documented assumption.

inline std::vector<uint8_t> encode_mms_read_request(avtp::ByteBusId byte_bus_id, MdioMmsAddr addr,
                                                       size_t word_count, uint8_t transaction_num) {
    if (!mms_addr_valid(addr)) return {};
    if (word_count == 0 || word_count > kMaxBurstWords) return {};

    std::vector<uint8_t> payload(detail::kMmsReadRequestPayloadLen);
    payload[0] = static_cast<uint8_t>(mms_mode_for_word_count(word_count));
    detail::put_mms_addr_prefix(&payload[detail::kModeOctetLen], addr);
    detail::put_be16(&payload[detail::kModeOctetLen + detail::kMmsAddrPrefixLen], static_cast<uint16_t>(word_count));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// Fails the same way decode_read_request() does; MdioErrc::wrong_mdio_mode
// if the decoded mdio_mode octet belongs to the MMD family instead (use
// decode_read_request()); MdioErrc::bad_mms_addr if the decoded address
// fails mms_addr_valid().
inline std::error_code decode_mms_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                 MdioMmsAddr& out_addr, size_t& out_word_count,
                                                 uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
    if (ec) return make_error_code(MdioErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);
    if (hdr.op) return make_error_code(MdioErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(MdioErrc::bad_evt);
    if (payload.size() < detail::kMmsReadRequestPayloadLen) return make_error_code(MdioErrc::short_frame);

    const auto mode = static_cast<MdioMode>(payload[0] & kModeOctetMask);
    if (!mode_is_unsupported_mms(mode)) return make_error_code(MdioErrc::wrong_mdio_mode);

    const MdioMmsAddr addr = detail::get_mms_addr_prefix(&payload[detail::kModeOctetLen]);
    if (!mms_addr_valid(addr)) return make_error_code(MdioErrc::bad_mms_addr);

    const uint16_t word_count = detail::get_be16(&payload[detail::kModeOctetLen + detail::kMmsAddrPrefixLen]);
    if (word_count == 0 || static_cast<size_t>(word_count) > kMaxBurstWords)
        return make_error_code(MdioErrc::bad_word_count);

    out_addr             = addr;
    out_word_count        = word_count;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_mms_read_response encodes a read response carrying
// mms_pack_words(mms, words, word_count) as its payload — mms is a
// caller-supplied input (not carried in the response payload itself; the
// caller already knows it from the originating request). Otherwise
// identical to encode_read_response().
inline std::vector<uint8_t> encode_mms_read_response(avtp::ByteBusId byte_bus_id, uint8_t mms,
                                                        const uint32_t* words, size_t word_count,
                                                        uint8_t transaction_num, bool timed, uint64_t timestamp) {
    if (word_count > kMaxBurstWords) return {};
    const std::vector<uint8_t> payload = mms_pack_words(mms, words, word_count);

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.rsp             = true;
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, payload);
    }
    return acf::encode_acf_abb(hdr, payload);
}

// decode_mms_read_response: mms is a caller-supplied INPUT used only to
// validate the payload's own byte length against mms's own word width via
// mms_word_count_of() — otherwise identical to decode_read_response().
inline std::error_code decode_mms_read_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                  uint8_t mms, std::vector<uint8_t>& out_words_data,
                                                  size_t& out_word_count, bool& out_timed, uint64_t& out_timestamp,
                                                  uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(MdioErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    bool                  timed     = false;
    uint64_t              timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);

    size_t word_count = 0;
    if (!mms_word_count_of(mms, payload.size(), word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_words_data       = std::move(payload);
    out_word_count       = word_count;
    out_timed            = timed;
    out_timestamp        = timestamp;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── MMS write request/response (REQ-MDIO-022/024) ───────────────────────────

inline std::vector<uint8_t> encode_mms_write_request(avtp::ByteBusId byte_bus_id, MdioMmsAddr addr,
                                                        const uint32_t* words, size_t word_count,
                                                        uint8_t transaction_num) {
    if (!mms_addr_valid(addr)) return {};
    if (word_count == 0 || word_count > kMaxBurstWords) return {};

    const std::vector<uint8_t> words_bytes = mms_pack_words(addr.mms, words, word_count);
    std::vector<uint8_t>       payload(detail::kMmsWriteRequestMinPayloadLen + words_bytes.size());
    payload[0] = static_cast<uint8_t>(mms_mode_for_word_count(word_count));
    detail::put_mms_addr_prefix(&payload[detail::kModeOctetLen], addr);
    std::copy(words_bytes.begin(), words_bytes.end(),
              payload.begin() + static_cast<long>(detail::kMmsWriteRequestMinPayloadLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// Fails the same way decode_mms_read_request() does (with
// MdioErrc::wrong_op instead of a read-op check, matching
// decode_write_request()'s own convention). MdioErrc::bad_word_count covers
// a words-region byte length that is not a whole multiple of the decoded
// addr.mms's own word width, is 0, or represents more than kMaxBurstWords
// words.
inline std::error_code decode_mms_write_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                   MdioMmsAddr& out_addr, std::vector<uint8_t>& out_words_data,
                                                   size_t& out_word_count, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
    if (ec) return make_error_code(MdioErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);
    if (!hdr.op) return make_error_code(MdioErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(MdioErrc::bad_evt);
    if (payload.size() < detail::kMmsWriteRequestMinPayloadLen) return make_error_code(MdioErrc::short_frame);

    const auto mode = static_cast<MdioMode>(payload[0] & kModeOctetMask);
    if (!mode_is_unsupported_mms(mode)) return make_error_code(MdioErrc::wrong_mdio_mode);

    const MdioMmsAddr addr = detail::get_mms_addr_prefix(&payload[detail::kModeOctetLen]);
    if (!mms_addr_valid(addr)) return make_error_code(MdioErrc::bad_mms_addr);

    const size_t words_len = payload.size() - detail::kMmsWriteRequestMinPayloadLen;
    size_t       word_count = 0;
    if (!mms_word_count_of(addr.mms, words_len, word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count == 0 || word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_addr       = addr;
    out_words_data = std::vector<uint8_t>(payload.begin() + static_cast<long>(detail::kMmsWriteRequestMinPayloadLen),
                                           payload.end());
    out_word_count       = word_count;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_mms_write_response: mms is a caller-supplied input, the same
// convention as encode_mms_read_response(). Otherwise identical to
// encode_write_response().
inline std::vector<uint8_t> encode_mms_write_response(avtp::ByteBusId byte_bus_id, uint8_t mms,
                                                         const uint32_t* accepted_words,
                                                         size_t accepted_word_count, uint8_t transaction_num,
                                                         bool timed, uint64_t timestamp) {
    if (accepted_word_count > kMaxBurstWords) return {};
    const std::vector<uint8_t> payload = mms_pack_words(mms, accepted_words, accepted_word_count);

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.rsp             = true;
    hdr.evt_op          = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, payload);
    }
    return acf::encode_acf_abb(hdr, payload);
}

// decode_mms_write_response: mms is a caller-supplied input, the same
// convention as decode_mms_read_response(). Otherwise identical to
// decode_write_response().
inline std::error_code decode_mms_write_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                   uint8_t mms, std::vector<uint8_t>& out_words_data,
                                                   size_t& out_word_count, bool& out_timed, uint64_t& out_timestamp,
                                                   uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(MdioErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    bool                  timed     = false;
    uint64_t              timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(MdioErrc::short_frame);
        if (ec) return make_error_code(MdioErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(MdioErrc::wrong_bus);

    size_t word_count = 0;
    if (!mms_word_count_of(mms, payload.size(), word_count)) return make_error_code(MdioErrc::bad_word_count);
    if (word_count > kMaxBurstWords) return make_error_code(MdioErrc::bad_word_count);

    out_words_data       = std::move(payload);
    out_word_count       = word_count;
    out_timed            = timed;
    out_timestamp        = timestamp;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── MdioEndpoint convenience wrapper (source-compatible with rcp/mock.hpp) ──
// MdioRequest/MdioResponse/MdioEndpoint below are UNCHANGED in field shape
// and method signature from before this pass — rcp/mock.hpp's dispatch_mdio()
// constructs MdioRequest{mode, mdio_address, is_write, mdio_payload} and
// calls mdio_.handle_request(req.evt_op, request, response) with exactly
// this shape; both keep working unmodified by this port.
//
// REFRAMED (this pass): the header comment that used to accompany this
// section claimed mdio_mode/mdio_address/mdio_payload as "the real
// addressing model" per Table 57/60 — that claim is false against c-RCP's
// actual design (see this file's own top-of-file "MAJOR CONTENT-DRIFT FIX"
// section) and is corrected here. mdio_address below is honestly this
// wrapper's OWN simplified, opaque round-trip key — deliberately NOT
// decomposed into the real Clause-22/Clause-45 prtad/devad/regad split (or
// the real MMS mms/addr split) the ACF-level codec above now implements
// faithfully. This wrapper exists purely as a self-contained (mode,
// mdio_address)-keyed register-map convenience for rcp/mock.hpp's own
// in-process simulator (see mock.hpp's own "No set_mdio_response()" note) —
// a caller that needs TC18-conformant wire encoding uses the free functions
// above instead (encode_read_request()/decode_read_request()/etc.).
//
// TriggerRegistry removed (drift fix): see the file header's own "No
// trigger-signal table" section — c-RCP documents no MDIO trigger table at
// all, mirroring rcp/can.hpp's CanEndpoint, which this class now matches.

struct MdioRequest {
    MdioMode mode          = MdioMode::MmdSingleWord;
    uint16_t mdio_address  = 0; // this wrapper's own opaque round-trip key — see comment above
    bool     mms_is_0_or_1 = false; // only meaningful when mode == MmsMultiWord; see payload_width_bits
    bool     is_write      = false;
    uint32_t mdio_payload  = 0; // width per payload_width_bits(mode, mms_is_0_or_1)
};

struct MdioResponse {
    uint32_t mdio_payload = 0;
};

inline std::error_code validate_request(const MdioRequest& req) noexcept {
    const uint8_t  width     = payload_width_bits(req.mode, req.mms_is_0_or_1);
    const uint64_t max_value = (width == 32) ? 0xFFFFFFFFull : 0xFFFFull;
    if (req.mdio_payload > max_value) return make_error_code(MdioErrc::payload_exceeds_mode_width);
    return {};
}

// register_key folds (mode, mdio_address) into one lookup key, so the four
// mdio_mode values never collide with each other even for an identical
// mdio_address bit pattern.
class MdioEndpoint {
public:
    // transact is MDIO's own pre-existing single-call register access — a
    // write stores req.mdio_payload under (mode, mdio_address) and echoes
    // it back via `out`; a read returns whatever was last stored there, or
    // 0 if never written.
    std::error_code transact(MdioRequest req, MdioResponse& out) {
        auto ec = validate_request(req);
        if (ec) return ec;

        last_request_ = req;
        const uint64_t key = register_key(req);
        if (req.is_write) {
            registers_[key] = req.mdio_payload;
            out.mdio_payload = req.mdio_payload;
        } else {
            const auto it = registers_.find(key);
            out.mdio_payload = (it != registers_.end()) ? it->second : uint32_t{0};
        }
        return {};
    }

    // handle_request classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of before doing anything else, so a
    // Reserved value can never reach transact() and be misread as an
    // ordinary transaction, and a ConfigWrite value can never be silently
    // accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transact(request,
    //     out) with both arguments unchanged.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without recording
    //     anything (last_request_ is left exactly as it was, no register is
    //     written or read).
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the MDIO EP's own functional-config block
    //     (apply_reconfig()/render_registers() above), not an
    //     mdio_mode-selected register access at all — returns
    //     MdioErrc::config_write_not_supported.
    std::error_code handle_request(uint8_t evt_op, MdioRequest req, MdioResponse& out) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transact(std::move(req), out);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(MdioErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const MdioRequest& last_request() const noexcept { return last_request_; }

private:
    static uint64_t register_key(const MdioRequest& req) noexcept {
        return (static_cast<uint64_t>(req.mode) << 16) | req.mdio_address;
    }

    MdioRequest                            last_request_;
    std::unordered_map<uint64_t, uint32_t> registers_;
};

} // namespace mdio
} // namespace rcp

// Enable std::error_code construction from rcp::mdio::MdioErrc/MdioReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::mdio::MdioErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::mdio::MdioReconfigErrc> : true_type {};
} // namespace std
