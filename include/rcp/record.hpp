// fusa:req REQ-REC-001
// fusa:req REQ-REC-002
// fusa:req REQ-REC-003
// fusa:req REQ-REC-004
// fusa:req REQ-REC-005
// fusa:req REQ-REC-006
// fusa:req REQ-REC-007
// fusa:req REQ-REC-008

// Binary record and replay of RCP traffic (v0.27.0).
//
// RecordingController wraps any Controller and writes a timestamped binary log
// of every Command/Response pair to a Record.  Playback::send() reads the log
// and drives a mock controller with the recorded responses.
//
// Log format per entry (little-endian):
//   [8 bytes] wall-clock timestamp in nanoseconds since epoch
//   [2 bytes] CommandType
//   [1 byte]  Zone
//   [1 byte]  Priority
//   [4 bytes] payload length
//   [N bytes] payload
//   [1 byte]  ResponseStatus
//   [4 bytes] response payload length
//   [N bytes] response payload
#pragma once

#include "rcp.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace record {

// ── Entry ─────────────────────────────────────────────────────────────────────

struct Entry {
    int64_t         timestamp_ns; // nanoseconds since epoch
    Command         cmd;
    Response        resp;
    std::error_code error;
};

// ── Record ────────────────────────────────────────────────────────────────────

class Record {
public:
    void append(Entry e) {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.push_back(std::move(e));
    }

    const std::vector<Entry>& entries() const noexcept { return entries_; }
    size_t size() const noexcept { return entries_.size(); }

    // write_binary serialises the record to a file.
    std::error_code write_binary(const std::string& path) const {
        std::lock_guard<std::mutex> lk(mu_);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return std::make_error_code(std::errc::io_error);
        for (auto& e : entries_) {
            auto write8 = [&](int64_t v) {
                f.write(reinterpret_cast<const char*>(&v), 8);
            };
            auto write2 = [&](uint16_t v) {
                f.write(reinterpret_cast<const char*>(&v), 2);
            };
            auto write1 = [&](uint8_t v) {
                f.write(reinterpret_cast<const char*>(&v), 1);
            };
            auto write4 = [&](uint32_t v) {
                f.write(reinterpret_cast<const char*>(&v), 4);
            };
            auto writeVec = [&](const std::vector<uint8_t>& d) {
                write4(static_cast<uint32_t>(d.size()));
                if (!d.empty())
                    f.write(reinterpret_cast<const char*>(d.data()),
                            static_cast<std::streamsize>(d.size()));
            };

            write8(e.timestamp_ns);
            write2(static_cast<uint16_t>(e.cmd.type));
            write1(static_cast<uint8_t>(e.cmd.zone));
            write1(static_cast<uint8_t>(e.cmd.priority));
            writeVec(e.cmd.payload);
            write1(static_cast<uint8_t>(e.resp.status));
            writeVec(e.resp.payload);
        }
        return {};
    }

private:
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
};

// ── RecordingController ───────────────────────────────────────────────────────

class RecordingController final : public rcp::Controller {
public:
    RecordingController(std::shared_ptr<rcp::Controller> inner,
                         std::shared_ptr<Record>           rec)
        : inner_(std::move(inner)), rec_(std::move(rec)) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        auto ec = inner_->send(ctx, cmd, out);

        Entry e;
        e.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        e.cmd   = cmd;
        e.resp  = out;
        e.error = ec;
        rec_->append(std::move(e));
        return ec;
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override { return inner_->close(); }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::shared_ptr<Record>          rec_;
};

// ── PlaybackConfig ────────────────────────────────────────────────────────────

struct PlaybackConfig {
    double speed_factor = 1.0; // 2.0 = 2× faster, 0.0 = no delays
};

// ── Playback ──────────────────────────────────────────────────────────────────

// Playback replays a Record against a target controller using the recorded
// inter-entry timing (scaled by speed_factor).
class Playback {
public:
    Playback(std::shared_ptr<rcp::Controller> target,
              const Record&                    rec,
              PlaybackConfig                   cfg = {})
        : target_(std::move(target)), rec_(rec), cfg_(cfg) {}

    // run_all replays every entry synchronously, pausing between entries to
    // respect the original timing (adjusted by speed_factor).
    std::error_code run_all(const Context& ctx) {
        const auto& entries = rec_.entries();
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

            Response out;
            auto ec = target_->send(ctx, e.cmd, out);
            (void)ec;
        }
        return {};
    }

private:
    std::shared_ptr<rcp::Controller> target_;
    const Record&                    rec_;
    PlaybackConfig                   cfg_;
};

inline std::shared_ptr<RecordingController> new_controller(
        std::shared_ptr<rcp::Controller> inner,
        std::shared_ptr<Record>           rec) {
    return std::make_shared<RecordingController>(std::move(inner), std::move(rec));
}

} // namespace record
} // namespace rcp
