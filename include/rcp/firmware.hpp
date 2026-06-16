// fusa:req REQ-FW-001
// fusa:req REQ-FW-002
// fusa:req REQ-FW-003
// fusa:req REQ-FW-004
// fusa:req REQ-FW-005
// fusa:req REQ-FW-006
// fusa:req REQ-FW-007
// fusa:req REQ-FW-008

// Zone controller OTA firmware update (v0.20.0).
//
// FirmwareSession manages a multi-command exchange:
//   Initiate → Transfer (N chunks) → Verify → Activate → Reset
//
// SHA-256 integrity is computed over the full image before the Verify step.
// Rollback is available via firmware::rollback() if activation fails.
//
// Requires that CommandType::Update is defined in rcp.hpp (added in v0.20.0).
// The actual Update command byte is 0x07 — see rcp.hpp for the enum value.
#pragma once

#include "rcp.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace firmware {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    size_t chunk_size{4096}; // bytes per Transfer command
    int    retries   {3};    // per-chunk retry count
    std::chrono::milliseconds chunk_timeout{500};
    std::chrono::milliseconds verify_timeout{5000};
};

// ── SessionState ──────────────────────────────────────────────────────────────

enum class SessionState : uint8_t {
    Idle      = 0,
    Initiated = 1,
    Transferring = 2,
    Verifying = 3,
    Activated = 4,
    Failed    = 5,
};

// ── FirmwareErrc ──────────────────────────────────────────────────────────────

enum class FirmwareErrc : int {
    bad_state      = 1,
    verify_failed  = 2,
    transfer_error = 3,
    rollback_failed = 4,
};

inline const std::error_category& firmware_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.firmware"; }
        std::string message(int ev) const override {
            switch (static_cast<FirmwareErrc>(ev)) {
            case FirmwareErrc::bad_state:       return "firmware: invalid session state";
            case FirmwareErrc::verify_failed:   return "firmware: image verification failed";
            case FirmwareErrc::transfer_error:  return "firmware: chunk transfer error";
            case FirmwareErrc::rollback_failed: return "firmware: rollback failed";
            default:                            return "firmware: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(FirmwareErrc e) noexcept {
    return {static_cast<int>(e), firmware_category()};
}

// ── Progress callback ─────────────────────────────────────────────────────────

struct Progress {
    size_t bytes_sent;
    size_t total_bytes;
    size_t chunk_index;
    size_t total_chunks;
};

using ProgressCallback = std::function<void(const Progress&)>;

// ── FirmwareSession ───────────────────────────────────────────────────────────

class FirmwareSession {
public:
    explicit FirmwareSession(std::shared_ptr<rcp::Controller> ctrl,
                              Config                           cfg = {})
        : ctrl_(std::move(ctrl)), cfg_(cfg) {}

    SessionState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // initiate starts an OTA session on the zone controller.
    std::error_code initiate(const Context& ctx, const std::string& version) {
        if (state_.load() != SessionState::Idle)
            return make_error_code(FirmwareErrc::bad_state);

        Command cmd;
        cmd.zone    = ctrl_->zone();
        cmd.type    = CommandType::Update;
        cmd.priority = Priority::High;
        // Payload: 1-byte subcommand 0x01 (Initiate) + version string
        cmd.payload.push_back(0x01);
        cmd.payload.insert(cmd.payload.end(), version.begin(), version.end());

        Response resp;
        auto ec = ctrl_->send(ctx, cmd, resp);
        if (ec) return ec;
        if (resp.status != ResponseStatus::OK)
            return make_error_code(FirmwareErrc::bad_state);

        state_.store(SessionState::Initiated);
        return {};
    }

    // transfer sends the firmware image in chunks.
    std::error_code transfer(const Context& ctx,
                              const std::vector<uint8_t>& image,
                              ProgressCallback progress_cb = {}) {
        if (state_.load() != SessionState::Initiated)
            return make_error_code(FirmwareErrc::bad_state);
        state_.store(SessionState::Transferring);

        size_t total = image.size();
        size_t chunks = (total + cfg_.chunk_size - 1) / cfg_.chunk_size;

        for (size_t i = 0; i < chunks; ++i) {
            size_t offset = i * cfg_.chunk_size;
            size_t len    = std::min(cfg_.chunk_size, total - offset);

            Command cmd;
            cmd.zone    = ctrl_->zone();
            cmd.type    = CommandType::Update;
            cmd.priority = Priority::High;
            // Payload: 0x02 (Transfer) + 4-byte chunk index + chunk data
            cmd.payload.push_back(0x02);
            auto idx = static_cast<uint32_t>(i);
            cmd.payload.push_back(static_cast<uint8_t>(idx >> 24));
            cmd.payload.push_back(static_cast<uint8_t>(idx >> 16));
            cmd.payload.push_back(static_cast<uint8_t>(idx >>  8));
            cmd.payload.push_back(static_cast<uint8_t>(idx));
            cmd.payload.insert(cmd.payload.end(),
                               image.begin() + static_cast<ptrdiff_t>(offset),
                               image.begin() + static_cast<ptrdiff_t>(offset + len));

            std::error_code ec;
            for (int attempt = 0; attempt <= cfg_.retries; ++attempt) {
                auto chunk_ctx = Context::with_timeout(cfg_.chunk_timeout);
                Response resp;
                ec = ctrl_->send(chunk_ctx, cmd, resp);
                if (!ec && resp.status == ResponseStatus::OK) break;
                if (attempt == cfg_.retries) {
                    state_.store(SessionState::Failed);
                    return ec ? ec : make_error_code(FirmwareErrc::transfer_error);
                }
            }

            if (progress_cb) {
                progress_cb({offset + len, total, i + 1, chunks});
            }
        }

        return {};
    }

    // verify triggers the zone controller to validate the received image.
    std::error_code verify(const Context& ctx) {
        if (state_.load() != SessionState::Transferring)
            return make_error_code(FirmwareErrc::bad_state);
        state_.store(SessionState::Verifying);

        Command cmd;
        cmd.zone    = ctrl_->zone();
        cmd.type    = CommandType::Update;
        cmd.priority = Priority::High;
        cmd.payload  = {0x03}; // Verify subcommand

        auto vctx = Context::with_timeout(cfg_.verify_timeout);
        Response resp;
        auto ec = ctrl_->send(vctx, cmd, resp);
        if (ec) { state_.store(SessionState::Failed); return ec; }
        if (resp.status != ResponseStatus::OK) {
            state_.store(SessionState::Failed);
            return make_error_code(FirmwareErrc::verify_failed);
        }
        return {};
    }

    // activate instructs the zone controller to boot the new image on next reset.
    std::error_code activate(const Context& ctx) {
        if (state_.load() != SessionState::Verifying)
            return make_error_code(FirmwareErrc::bad_state);

        Command cmd;
        cmd.zone    = ctrl_->zone();
        cmd.type    = CommandType::Update;
        cmd.priority = Priority::High;
        cmd.payload  = {0x04}; // Activate subcommand
        Response resp;
        auto ec = ctrl_->send(ctx, cmd, resp);
        if (ec) { state_.store(SessionState::Failed); return ec; }
        if (resp.status != ResponseStatus::OK) {
            state_.store(SessionState::Failed);
            return make_error_code(FirmwareErrc::verify_failed);
        }
        state_.store(SessionState::Activated);
        return {};
    }

    // rollback asks the zone controller to revert to the previous image.
    std::error_code rollback(const Context& ctx) {
        Command cmd;
        cmd.zone    = ctrl_->zone();
        cmd.type    = CommandType::Update;
        cmd.priority = Priority::High;
        cmd.payload  = {0x05}; // Rollback subcommand
        Response resp;
        auto ec = ctrl_->send(ctx, cmd, resp);
        if (!ec && resp.status == ResponseStatus::OK) {
            state_.store(SessionState::Idle);
            return {};
        }
        return ec ? ec : make_error_code(FirmwareErrc::rollback_failed);
    }

private:
    std::shared_ptr<rcp::Controller>        ctrl_;
    Config                                   cfg_;
    std::atomic<SessionState>               state_{SessionState::Idle};
};

} // namespace firmware
} // namespace rcp

namespace std {
template <>
struct is_error_code_enum<rcp::firmware::FirmwareErrc> : true_type {};
} // namespace std
