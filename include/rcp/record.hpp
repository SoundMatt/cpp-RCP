// fusa:req REQ-REC-001
// fusa:req REQ-REC-002
// fusa:req REQ-REC-003
// fusa:req REQ-REC-004
// fusa:req REQ-REC-005
// fusa:req REQ-REC-006
// fusa:req REQ-REC-007
// fusa:req REQ-REC-008

// Binary record and replay of RC-Client-level request/response traffic.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `record.hpp` — its on-disk entry format is
// reworked to capture RC-Client-level request/response pairs (each
// rcp::acf::AcfMessageInfo header plus its payload, per rcp/acf.hpp,
// v2.0.0) instead of the pre-replacement Command/Response/Status/Priority
// tuples. `RequestFn` below is shaped identically to rcp/udp.hpp's
// Client::request's core signature (minus the TSCF-specific trailing
// knobs, which are a framing detail this transport-agnostic layer does not
// need to see) so a rcp::udp::Client instance, or any other client-side
// send-equivalent call, can be wrapped without adapting its call shape —
// the same generic-callable pattern rcp/observe.hpp's ObservingClient uses
// alongside this file at the same milestone.
//
// RecordingClient wraps a RequestFn and writes a timestamped binary log of
// every request/response pair to a Record. Playback::run_all() reads a
// Record back and drives a target RequestFn (e.g. a mock, per
// rcp/mock.hpp) with the recorded requests, honoring the recorded
// inter-entry timing.
//
// Full bit-for-bit conformance of the on-disk format against any other
// implementation is not claimed — it is this implementation's own logging
// encoding, same as the equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp,
// and rcp/discovery.hpp.
#pragma once

#include "acf.hpp"
#include "rcp.hpp" // for rcp::Context only — see this header's own scope note above

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace record {

// RequestFn is the "client-side send-equivalent call" this header wraps —
// see this file's header comment for why it mirrors udp::Client::request's
// core shape rather than depending on rcp/udp.hpp directly.
using RequestFn = std::function<std::error_code(const rcp::Context&,
                                                  const acf::AcfMessageInfo&,
                                                  const std::vector<uint8_t>&,
                                                  acf::AcfMessageInfo&,
                                                  std::vector<uint8_t>&)>;

// ── Entry ─────────────────────────────────────────────────────────────────────

struct Entry {
    int64_t               timestamp_ns = 0; // nanoseconds since epoch
    acf::AcfMessageInfo    request;
    std::vector<uint8_t>   request_payload;
    acf::AcfMessageInfo    response;
    std::vector<uint8_t>   response_payload;
    std::error_code        error;
};

// ── Record ────────────────────────────────────────────────────────────────────

class Record {
public:
    void append(Entry e) {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.push_back(std::move(e));
    }

    std::vector<Entry> entries() const {
        std::lock_guard<std::mutex> lk(mu_);
        return entries_;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return entries_.size();
    }

    // write_binary serialises the record to a file. Each entry's request
    // and response are encoded via rcp::acf::encode_acf_abb/encode_acf_gbb
    // (selected by AcfMessageInfo::acf_msg_type) — the same codec the wire
    // path itself uses — length-prefixed so read_binary can recover them
    // without re-deriving ACF's own length field semantics.
    std::error_code write_binary(const std::string& path) const {
        std::vector<Entry> snapshot;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snapshot = entries_;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return std::make_error_code(std::errc::io_error);
        for (auto& e : snapshot) write_entry(f, e);
        return f.good() ? std::error_code{} : std::make_error_code(std::errc::io_error);
    }

    // read_binary is write_binary's inverse: it replaces this Record's
    // entries with the ones decoded from `path`.
    std::error_code read_binary(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::make_error_code(std::errc::io_error);

        std::vector<Entry> loaded;
        while (f.peek() != std::ifstream::traits_type::eof()) {
            Entry e;
            if (auto ec = read_entry(f, e)) return ec;
            loaded.push_back(std::move(e));
        }
        std::lock_guard<std::mutex> lk(mu_);
        entries_ = std::move(loaded);
        return {};
    }

private:
    static void write8(std::ofstream& f, int64_t v)  { f.write(reinterpret_cast<const char*>(&v), 8); }
    static void write1(std::ofstream& f, uint8_t v)   { f.write(reinterpret_cast<const char*>(&v), 1); }
    static void write4(std::ofstream& f, uint32_t v)  { f.write(reinterpret_cast<const char*>(&v), 4); }
    static void write_msg(std::ofstream& f, const acf::AcfMessageInfo& info,
                           const std::vector<uint8_t>& payload) {
        // RequestFn (see this file's header comment) does not carry ACF_GBB's
        // separate message_timestamp parameter, only AcfMessageInfo/payload —
        // a GBB entry's recorded message_timestamp slot is therefore always
        // zero on replay. Harmless for this module's own record/replay round
        // trip (mtv/opcode-repurposing semantics, rcp/request.hpp, still
        // survive intact); a caller needing exact GBB timestamp fidelity
        // should record at a layer that sees it directly instead.
        std::vector<uint8_t> encoded = (info.acf_msg_type == acf::kAcfMsgTypeGbb)
            ? acf::encode_acf_gbb(info, /*message_timestamp=*/0, payload)
            : acf::encode_acf_abb(info, payload);
        write4(f, static_cast<uint32_t>(encoded.size()));
        if (!encoded.empty())
            f.write(reinterpret_cast<const char*>(encoded.data()),
                    static_cast<std::streamsize>(encoded.size()));
    }

    static void write_entry(std::ofstream& f, const Entry& e) {
        write8(f, e.timestamp_ns);
        write_msg(f, e.request,  e.request_payload);
        write_msg(f, e.response, e.response_payload);
        write4(f, static_cast<uint32_t>(e.error.value()));
        write1(f, e.error ? 1 : 0);
    }

    static std::error_code read8(std::ifstream& f, int64_t& v) {
        f.read(reinterpret_cast<char*>(&v), 8);
        return f.good() ? std::error_code{} : std::make_error_code(std::errc::io_error);
    }
    static std::error_code read1(std::ifstream& f, uint8_t& v) {
        f.read(reinterpret_cast<char*>(&v), 1);
        return f.good() ? std::error_code{} : std::make_error_code(std::errc::io_error);
    }
    static std::error_code read4(std::ifstream& f, uint32_t& v) {
        f.read(reinterpret_cast<char*>(&v), 4);
        return f.good() ? std::error_code{} : std::make_error_code(std::errc::io_error);
    }
    static std::error_code read_msg(std::ifstream& f, acf::AcfMessageInfo& info,
                                     std::vector<uint8_t>& payload) {
        uint32_t len = 0;
        if (auto ec = read4(f, len)) return ec;
        std::vector<uint8_t> encoded(len);
        if (len > 0) {
            f.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(len));
            if (!f.good()) return std::make_error_code(std::errc::io_error);
        }
        if (!encoded.empty() && encoded[0] == acf::kAcfMsgTypeGbb) {
            uint64_t ts = 0;
            return acf::decode_acf_gbb(encoded.data(), encoded.size(), info, ts, payload);
        }
        return acf::decode_acf_abb(encoded.data(), encoded.size(), info, payload);
    }

    static std::error_code read_entry(std::ifstream& f, Entry& e) {
        if (auto ec = read8(f, e.timestamp_ns)) return ec;
        if (auto ec = read_msg(f, e.request,  e.request_payload))  return ec;
        if (auto ec = read_msg(f, e.response, e.response_payload)) return ec;
        uint32_t err_value = 0;
        uint8_t  has_error = 0;
        if (auto ec = read4(f, err_value)) return ec;
        if (auto ec = read1(f, has_error)) return ec;
        e.error = has_error
            ? std::error_code(static_cast<int>(err_value), std::generic_category())
            : std::error_code{};
        return {};
    }

    mutable std::mutex mu_;
    std::vector<Entry> entries_;
};

// ── RecordingClient ───────────────────────────────────────────────────────────

class RecordingClient {
public:
    RecordingClient(RequestFn inner, std::shared_ptr<Record> rec)
        : inner_(std::move(inner)), rec_(std::move(rec)) {}

    std::error_code request(const rcp::Context& ctx,
                             const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload) {
        auto ec = inner_(ctx, req, req_payload, out_resp, out_resp_payload);

        Entry e;
        e.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        e.request          = req;
        e.request_payload  = req_payload;
        e.response         = out_resp;
        e.response_payload = out_resp_payload;
        e.error            = ec;
        rec_->append(std::move(e));
        return ec;
    }

private:
    RequestFn                inner_;
    std::shared_ptr<Record>  rec_;
};

// ── PlaybackConfig ────────────────────────────────────────────────────────────

struct PlaybackConfig {
    double speed_factor = 1.0; // 2.0 = 2x faster, 0.0 = no delays
};

// ── Playback ──────────────────────────────────────────────────────────────────

// Playback replays a Record against a target RequestFn using the recorded
// inter-entry timing (scaled by speed_factor).
class Playback {
public:
    Playback(RequestFn target, const Record& rec, PlaybackConfig cfg = {})
        : target_(std::move(target)), rec_(rec), cfg_(cfg) {}

    // run_all replays every entry synchronously, pausing between entries to
    // respect the original timing (adjusted by speed_factor).
    std::error_code run_all(const Context& ctx) {
        auto entries = rec_.entries();
        if (entries.empty()) return {};

        auto prev_ts = entries.front().timestamp_ns;

        for (auto& e : entries) {
            int64_t gap_ns = e.timestamp_ns - prev_ts;
            if (gap_ns > 0 && cfg_.speed_factor > 0.0) {
                auto delay = std::chrono::nanoseconds(
                    static_cast<int64_t>(static_cast<double>(gap_ns) / cfg_.speed_factor));
                if (delay > std::chrono::milliseconds(1)) {
                    std::this_thread::sleep_for(delay);
                }
            }
            prev_ts = e.timestamp_ns;

            acf::AcfMessageInfo   out_resp;
            std::vector<uint8_t>  out_resp_payload;
            auto ec = target_(ctx, e.request, e.request_payload, out_resp, out_resp_payload);
            (void)ec;
        }
        return {};
    }

private:
    RequestFn      target_;
    const Record&  rec_;
    PlaybackConfig cfg_;
};

inline std::shared_ptr<RecordingClient> new_recording_client(RequestFn inner,
                                                                std::shared_ptr<Record> rec) {
    return std::make_shared<RecordingClient>(std::move(inner), std::move(rec));
}

} // namespace record
} // namespace rcp
