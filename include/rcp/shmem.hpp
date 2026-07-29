// fusa:req REQ-SHMEM-001
// fusa:req REQ-SHMEM-002
// fusa:req REQ-SHMEM-003
// fusa:req REQ-SHMEM-004
// fusa:req REQ-SHMEM-005
// fusa:req REQ-SHMEM-006
// fusa:req REQ-SHMEM-007
// fusa:req REQ-SHMEM-008

// Zero-copy intra-host request delivery via shared in-process memory.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `shmem.hpp` — the "avoid serialisation
// overhead for a co-located RC Client/RC Server" value proposition is
// unaffected by the protocol replacement, so the concept survives; only
// the request/response shapes it carries change. shmem::Channel and
// shmem::Registry below play the same role rcp/udp.hpp's Server/Client and
// Registry-shaped lookup play for a real socket transport, just for two
// endpoints that happen to live in the same process: no bytes are ever
// serialised to (or decoded from) an AVTPDU/ACF wire encoding, since both
// sides already share the same acf::AcfMessageInfo/std::vector<uint8_t>
// objects in memory.
//
// This header has no clock, thread, or socket of its own — Channel::
// request() dispatches synchronously on the calling thread, same
// "primitives driven by the embedding application" convention every Phase
// 14/15 header has used since v2.9.0.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::ErrClosed/ErrNotFound/ErrAlreadyExists only — see this header's own scope note above

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace rcp {
namespace shmem {

// ── Channel ───────────────────────────────────────────────────────────────────

// Channel is the in-process, zero-copy analog of rcp/udp.hpp's Server:
// a caller-supplied Handler answers each request directly, in the same
// process, with no wire encode/decode step in between. Handler is shaped
// identically to udp::Server::Handler so the same handler (e.g.
// rcp::mock::Server::dispatch) can be wired to either transport.
class Channel {
public:
    using Handler = std::function<std::error_code(size_t client,
                                                    const acf::AcfMessageInfo& req,
                                                    const std::vector<uint8_t>& req_payload,
                                                    acf::AcfMessageInfo& out_resp,
                                                    std::vector<uint8_t>& out_resp_payload)>;

    explicit Channel(uint64_t stream_key) : stream_key_(stream_key) {}

    uint64_t stream_key() const noexcept { return stream_key_; }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
    }

    // request delivers `req`/`req_payload` straight to the registered
    // Handler and returns its response, with no serialisation step between
    // caller and handler. If no Handler is set, the default response is
    // ResponseKind::Acknowledge — the same "answer something, don't hang"
    // default rcp/udp.hpp's Server uses when unhandled.
    std::error_code request(size_t client, const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload) {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;

        std::lock_guard<std::mutex> lk(mu_);
        if (handler_) {
            return handler_(client, req, req_payload, out_resp, out_resp_payload);
        }
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        out_resp_payload.clear();
        return {};
    }

    // close is idempotent — safe to call more than once.
    void close() { closed_.store(true, std::memory_order_release); }

    bool ok() const noexcept { return !closed_.load(std::memory_order_acquire); }

private:
    uint64_t           stream_key_;
    std::atomic<bool>  closed_{false};
    std::mutex         mu_;
    Handler            handler_;
};

// ── Registry ──────────────────────────────────────────────────────────────────

// Registry looks up a Channel by its stream_key, the same opaque
// uint64_t-keyed lookup role rcp/watchdog.hpp's Manager and
// rcp/regmap.hpp's Ep0 already establish for this codebase (typically an
// avtp::StreamId::to_u64()).
class Registry {
public:
    std::error_code add_channel(std::shared_ptr<Channel> ch) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        if (channels_.count(ch->stream_key())) return ErrAlreadyExists;
        channels_[ch->stream_key()] = std::move(ch);
        return {};
    }

    std::error_code deregister(uint64_t stream_key) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = channels_.find(stream_key);
        if (it == channels_.end()) return ErrNotFound;
        auto ch = it->second;
        channels_.erase(it);
        lk.unlock();
        ch->close();
        return {};
    }

    std::error_code lookup(uint64_t stream_key, std::shared_ptr<Channel>& out) {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        auto it = channels_.find(stream_key);
        if (it == channels_.end()) return ErrNotFound;
        out = it->second;
        return {};
    }

    std::vector<std::shared_ptr<Channel>> channels() {
        std::shared_lock<std::shared_mutex> lk(mu_);
        std::vector<std::shared_ptr<Channel>> out;
        out.reserve(channels_.size());
        for (auto& kv : channels_) out.push_back(kv.second);
        return out;
    }

    // close is idempotent — safe to call more than once.
    std::error_code close() {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        auto local = std::move(channels_);
        lk.unlock();
        for (auto& kv : local) kv.second->close();
        return {};
    }

private:
    mutable std::shared_mutex                    mu_;
    std::map<uint64_t, std::shared_ptr<Channel>>  channels_;
    bool                                          closed_ = false;
};

inline std::shared_ptr<Channel> new_channel(uint64_t stream_key) {
    return std::make_shared<Channel>(stream_key);
}

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

} // namespace shmem
} // namespace rcp
