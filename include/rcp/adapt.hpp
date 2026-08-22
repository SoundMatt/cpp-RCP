// fusa:req REQ-RELAY-001
// fusa:req REQ-RELAY-002
// fusa:req REQ-RELAY-003
// fusa:req REQ-RELAY-004
// fusa:req REQ-RELAY-005

// RELAY application interface adapter for cpp-RCP (§10.3, §18.2).
//
// Adapt() wraps a client-side send-equivalent call as a relay::Caller so
// application code can use the protocol-agnostic relay::Node / relay::Caller
// interface and swap the underlying protocol with a single constructor
// change.
//
// Usage:
//   auto fn = [&](const rcp::Context& ctx, const rcp::acf::AcfMessageInfo& req,
//                 const std::vector<uint8_t>& payload,
//                 rcp::acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
//       return my_server.dispatch(0, req, payload, out, out_payload);
//   };
//   auto caller = rcp::Adapt(fn);            // relay::Caller*
//   relay::Message req;
//   req.id = rcp::endpoint_id_to_relay_id(byte_bus_id);
//   auto [resp, ec] = caller->call(ctx, req);
//
// ROADMAP.md milestone 60, "C ABI & CLI Rebuild (v2.16.0)": this header is
// ADAPTed in place, per the Satellite Package Disposition table's entry for
// `adapt.hpp` — Adapt()/ToMessage()/FromMessage() are rebound from
// rcp.hpp's pre-replacement Zone/Command/Controller model to the ACF
// request/response shape (rcp/acf.hpp, v2.0.0) established through v2.15.0.
// relay::Message addressing moves from a PascalCase zone name in
// relay::Message.id to a plain decimal ByteBusID there instead (see
// endpoint_id_to_relay_id/relay_id_to_endpoint_id below); relay/relay.hpp
// itself needs no change, per this milestone's own scope note. Adapt() now
// takes a RequestFn — the same "client-side send-equivalent call" shape
// rcp/record.hpp's and rcp/observe.hpp's own RequestFn already standardize
// on (v2.14.0) — rather than a shared_ptr<Controller>, the same "primitives,
// not a wrapped chokepoint" choice those two headers and every ADAPTed
// Phase 14/15 bridge already made; there is no unified client-side send()
// chokepoint left to wrap. subscribe() has no analog here and always
// reports std::errc::function_not_supported, the same call
// rcp/mqttbr.hpp's (and its six siblings') dropped subscribe()/
// StatusChannel method already made at v2.15.0 — it belonged to
// rcp::Controller's status-telemetry push model, which the target
// specification's request/response shape has no equivalent of.
//
// Message.id encoding (cpp-RCP-FS-05, #88): RELAY spec v2.0 §15.7.5 defines
// relay.Message.ID for RCP as just the decimal ByteBusID string (0-255) on
// its own; a Caller (and the Adapt() wrapping it) already presents one fixed
// StreamID identity per §8.5, so the stream/server identity is carried by
// the RequestFn's own binding (e.g. whatever connection or in-process server
// it closes over) rather than folded into Message.id. Prior to this fix,
// this file encoded "<stream_key as 16 lowercase hex digits>:<byte_bus_id>"
// into Message.id, which does not match the spec's plain decimal-string
// form and would have broken interop with anything expecting it.
//
// ── Phase 4 rewrite (cpp-RCP issue #129) — genuine content-drift fix found ──
// A prior scoping pass characterized this header's generic RequestFn
// passthrough design as fully behaviorally equivalent to c-RCP's much
// richer per-endpoint-type rcp_adapt_op_t/field-table model
// (c-RCP's include/rcp/adapt.h + src/adapt.c, this project's content
// source of truth for this module). A line-by-line behavioral comparison
// against every one of that file's 13 endpoint-type op-mappings confirmed
// the *shape* of that claim — this header's op/evt_op meta fields and raw
// `payload` passthrough do correctly generalize c-RCP's per-op GPIO/SPI/
// PWM_OUT channel-and-write-semantics handling (evt[2:0] is one shared ACF
// wire field regardless of which endpoint type is using it) — but found
// one real, load-bearing omission: this header never threaded the ACF
// header's own read_size_or_segment_num field through message_to_request()/
// response_to_message() at all, silently defaulting it to 0 for every
// relay::Message. c-RCP's own adapt.c threads the identical wire field
// through this same bridging layer via rcp.uart.read_size/rcp.spi.
// read_size/rcp.adc.read_size/rcp.i2c.read_size/rcp.iseled.read_size —
// five of its per-op table's rows depend on it, and for I2C/ISELED it is
// the ONLY thing that selects the read vs. write direction. Fixed below
// via a single generic "rcp.read_size" meta key (read_size_from_meta()) —
// see that function's own doc comment for the full citation trail.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/rcp.hpp> // for rcp::Context only — see this header's own scope note above
#include <relay/relay.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rcp {

// ── byte_bus_id ↔ relay::Message.ID helpers (§15.7.5) ─────────────────────────
// relay.Message.ID for RCP is just the decimal ByteBusID string (0-255) —
// the StreamID is not part of it (§15.7.5, §8.5: one StreamID per Caller
// instance).

// endpoint_id_to_relay_id encodes a byte_bus_id into a relay::Message.id
// string.
inline std::string endpoint_id_to_relay_id(avtp::ByteBusId byte_bus_id) {
    return std::to_string(static_cast<unsigned>(byte_bus_id));
}

// relay_id_to_endpoint_id decodes a relay::Message.id string produced by
// endpoint_id_to_relay_id back into a byte_bus_id. Returns false — leaving
// the output unspecified — for anything that isn't a bare decimal integer
// in [0, 255], including the pre-#88 "<16 hex digits>:<decimal>" form this
// replaces and the pre-v2.16.0 PascalCase zone-name form before that.
inline bool relay_id_to_endpoint_id(const std::string& id, avtp::ByteBusId& out_byte_bus_id) {
    if (id.empty()) return false;
    unsigned bus_id = 0;
    auto [p, ec] = std::from_chars(id.data(), id.data() + id.size(), bus_id, 10);
    if (ec != std::errc{} || p != id.data() + id.size() || bus_id > 0xFF) return false;
    out_byte_bus_id = static_cast<avtp::ByteBusId>(bus_id);
    return true;
}

// ── request/response field ↔ relay::Message.meta helpers ─────────────────────

// op_from_meta parses the "rcp.op" meta key ("read"/"write"; default read).
inline bool op_from_meta(const std::map<std::string, std::string>& meta) {
    auto it = meta.find("rcp.op");
    return it != meta.end() && it->second == "write";
}

// evt_op_from_meta parses the "rcp.evt_op" meta key (decimal 0-7; default 0)
// — the endpoint-defined sub-opcode carried in AcfMessageInfo::evt_op.
inline uint8_t evt_op_from_meta(const std::map<std::string, std::string>& meta) {
    auto it = meta.find("rcp.evt_op");
    if (it == meta.end()) return 0;
    unsigned v = 0;
    auto [p, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), v, 10);
    if (ec != std::errc{} || p != it->second.data() + it->second.size() || v > 7) return 0;
    return static_cast<uint8_t>(v);
}

// read_size_from_meta parses the "rcp.read_size" meta key (decimal 0-4095;
// default 0) — the ACF header's own 12-bit read_size_or_segment_num field
// (rcp/acf.hpp's AcfMessageInfo::read_size_or_segment_num).
//
// FIX (found during the cpp-RCP issue #129 c-RCP adapt.c/.h behavioral
// comparison): this helper, and the two call sites below that use it, were
// missing entirely before this pass — message_to_request() populated only
// op and evt_op from meta, silently leaving read_size_or_segment_num at
// AcfMessageInfo's own default of 0 for every relay::Message, with no way
// for a caller to override it. That is a real, load-bearing gap, not a
// cosmetic one: read_size_or_segment_num is the wire field a standard
// request's own read length rides on for every endpoint type that needs
// one — rcp/uart.hpp's encode_read_request ("read_size rides the ACF
// header's own read_size_or_segment_num field", uart.hpp kMaxReadSize =
// 0x0FFFu), rcp/spi.hpp's encode_transfer_request (read_size combines with
// the payload length via transfer_length()), rcp/adc.hpp's
// encode_read_request, and rcp/i2c.hpp's encode_transfer_request (where
// read_size == 0 IS the write direction and read_size != 0 selects the
// read direction — i.e. without this fix an I2C_TRANSFER built via Adapt()
// could never select the read direction at all). c-RCP's own adapt.c
// (this project's content source of truth for this module) threads the
// exact same wire field through this same bridging layer under distinct
// per-endpoint-type meta keys — rcp.uart.read_size (required, src/adapt.c
// rcp_message_to_request()'s RCP_ADAPT_OP_UART_READ case),
// rcp.spi.read_size (default = payload length, RCP_ADAPT_OP_SPI_TRANSFER
// case), rcp.adc.read_size (default = one value's worth,
// RCP_ADAPT_OP_ADC_READ case), and rcp.i2c.read_size /
// rcp.iseled.read_size (default 0 = write direction, RCP_ADAPT_OP_I2C_
// TRANSFER / _ISELED_COMMAND cases) — because, unlike this generic
// passthrough design, c-RCP's own per-op field table needs one key per
// endpoint-type family. A single generic "rcp.read_size" key is enough
// here because read_size_or_segment_num is one wire field shared by every
// endpoint type that has one, not a per-type concept — matching how
// "rcp.evt_op" above already covers GPIO_WRITE's evt, PWM_OUT_WRITE's evt,
// and SPI_TRANSFER's channel select generically, since all three are the
// same evt[2:0] wire field too.
inline uint16_t read_size_from_meta(const std::map<std::string, std::string>& meta) {
    auto it = meta.find("rcp.read_size");
    if (it == meta.end()) return 0;
    unsigned v = 0;
    auto [p, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), v, 10);
    if (ec != std::errc{} || p != it->second.data() + it->second.size() || v > 0x0FFFu) return 0;
    return static_cast<uint16_t>(v);
}

// ── ToMessage / FromMessage (§15.7.5) ────────────────────────────────────────

// response_to_message converts an ACF response (rcp/acf.hpp) into a
// relay::Message (call direction).
inline relay::Message response_to_message(const acf::AcfMessageInfo& resp,
                                           const std::vector<uint8_t>& payload) {
    relay::Message msg;
    msg.protocol  = relay::Protocol::RCP;
    msg.id        = endpoint_id_to_relay_id(resp.byte_bus_id);
    msg.payload   = payload;
    msg.timestamp = std::chrono::system_clock::now();
    msg.meta["rcp.response_kind"] = std::to_string(static_cast<int>(acf::response_kind_of(resp)));
    msg.meta["rcp.err"]           = resp.err ? "true" : "false";
    // rcp.read_size echoes resp.read_size_or_segment_num — see
    // read_size_from_meta()'s own doc comment for why this field matters
    // (UART/SPI/ADC/I2C/ISELED read length and, for I2C/ISELED, direction
    // selection) and why a request-side omission of it was a real bug.
    msg.meta["rcp.read_size"]     = std::to_string(resp.read_size_or_segment_num);
    return msg;
}

// message_to_request converts a relay::Message into an ACF standard request
// (call/send direction). Returns false — leaving all outputs unspecified —
// if msg.id does not decode as a byte_bus_id.
inline bool message_to_request(const relay::Message& msg,
                                acf::AcfMessageInfo& out_info, std::vector<uint8_t>& out_payload) {
    avtp::ByteBusId byte_bus_id = 0;
    if (!relay_id_to_endpoint_id(msg.id, byte_bus_id)) return false;
    out_info             = acf::AcfMessageInfo{};
    out_info.byte_bus_id = byte_bus_id;
    out_info.op          = op_from_meta(msg.meta);
    out_info.evt_op       = evt_op_from_meta(msg.meta);
    // read_size_or_segment_num — see read_size_from_meta()'s own doc
    // comment for why this was missing before this pass and why that was a
    // genuine behavioral gap, not a cosmetic one.
    out_info.read_size_or_segment_num = read_size_from_meta(msg.meta);
    out_payload           = msg.payload;
    return true;
}

// ── RequestFn — the client-side send-equivalent call this header wraps ──────
// Shaped identically to rcp/record.hpp's and rcp/observe.hpp's own RequestFn
// (v2.14.0) — see this file's header comment for why.
using RequestFn = std::function<std::error_code(const rcp::Context&,
                                                  const acf::AcfMessageInfo&,
                                                  const std::vector<uint8_t>&,
                                                  acf::AcfMessageInfo&,
                                                  std::vector<uint8_t>&)>;

// ── RcpCallerAdapter — implements relay::Caller over a RequestFn ─────────────

class RcpCallerAdapter final : public relay::Caller {
public:
    explicit RcpCallerAdapter(RequestFn fn) : fn_(std::move(fn)) {}

    relay::Protocol protocol() const noexcept override {
        return relay::Protocol::RCP;
    }

    // send maps relay::Message → ACF request, discards the response (§10.6).
    std::error_code send(relay::Context ctx, const relay::Message& msg) override {
        if (!fn_) return std::make_error_code(std::errc::not_connected);
        acf::AcfMessageInfo req;
        std::vector<uint8_t> payload;
        if (!message_to_request(msg, req, payload))
            return std::make_error_code(std::errc::invalid_argument);
        acf::AcfMessageInfo out_info;
        std::vector<uint8_t> out_payload;
        return fn_(ctx, req, payload, out_info, out_payload);
    }

    // call maps relay::Message → ACF request → relay::Message (§10.2).
    std::pair<relay::Message, std::error_code>
        call(relay::Context ctx, const relay::Message& req_msg) override {
        if (!fn_) return {{}, std::make_error_code(std::errc::not_connected)};
        acf::AcfMessageInfo req;
        std::vector<uint8_t> payload;
        if (!message_to_request(req_msg, req, payload))
            return {{}, std::make_error_code(std::errc::invalid_argument)};
        acf::AcfMessageInfo out_info;
        std::vector<uint8_t> out_payload;
        auto ec = fn_(ctx, req, payload, out_info, out_payload);
        if (ec) return {{}, ec};
        return {response_to_message(out_info, out_payload), {}};
    }

    // subscribe: no analog in the target specification's request/response
    // shape — see this header's own comment above.
    std::pair<std::shared_ptr<relay::Channel<relay::Message>>, std::error_code>
        subscribe(relay::SubscriberOptions /*opts*/ = {}) override {
        return {nullptr, std::make_error_code(std::errc::function_not_supported)};
    }

    // close: a RequestFn owns no resource this adapter can release on its
    // behalf — the same "still succeeds" choice rcp/mqttbr.hpp's (and its
    // six siblings') close() already makes at v2.15.0.
    std::error_code close() noexcept override {
        return {};
    }

private:
    RequestFn fn_;
};

// ── Adapt() (§10.3) ──────────────────────────────────────────────────────────

// Adapt wraps a RequestFn as a relay::Caller. The returned relay::Caller
// also satisfies relay::Node. Does NOT block or connect; wraps `fn`
// immediately. Per §8.5/§15.7.5, one Adapt()-wrapped Caller presents a
// single fixed StreamID identity — `fn` itself owns that binding (e.g. a
// connection it closes over) — and each relay::Message passed to
// send()/call() addresses one Endpoint on it via Message.id (see
// endpoint_id_to_relay_id above). An application juggling several RC
// Servers wraps each with its own Adapt() call (§8.5).
inline std::unique_ptr<relay::Caller> Adapt(RequestFn fn) {
    return std::make_unique<RcpCallerAdapter>(std::move(fn));
}

} // namespace rcp
