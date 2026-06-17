// RELAY C++ bindings — relay namespace types (RELAY spec §18.2, v0.2).
//
// Defines the protocol-agnostic relay:: namespace: Protocol, Message, Errc
// sentinels, Channel<T>, Context, SubscriberOptions, Node, and Caller.
// cpp-RCP conformance: include this header, then use rcp::Adapt() (adapt.hpp)
// to wrap a Controller as a relay::Caller.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace relay {

// ── Spec version (§19.4) ─────────────────────────────────────────────────────

constexpr std::string_view kRelaySpecVersion = "1.0";

// NOTE: keep in sync with RELAY spec/version.json. RELAY v1.0 (stable) carries
// no normative changes from v0.3 — it promotes the spec and freezes §5/§10/§12/
// §15. RCP's ToMessage()/FromMessage() mappings are unchanged.

// ── Protocol identifiers (§3) ────────────────────────────────────────────────

enum class Protocol : int {
    CAN    = 1,
    DDS    = 2,
    LIN    = 3,
    MQTT   = 4,
    RCP    = 5,
    SOMEIP = 6,
};

inline std::string to_string(Protocol p) {
    switch (p) {
    case Protocol::CAN:    return "CAN";
    case Protocol::DDS:    return "DDS";
    case Protocol::LIN:    return "LIN";
    case Protocol::MQTT:   return "MQTT";
    case Protocol::RCP:    return "RCP";
    case Protocol::SOMEIP: return "SOMEIP";
    default:               return "unknown";
    }
}

// ── Version (§4.1) ───────────────────────────────────────────────────────────

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// ── Message — universal envelope (§4) ────────────────────────────────────────

struct Message {
    Protocol                              protocol  = Protocol::RCP;
    Version                               version   = {};
    std::string                           id;
    std::vector<uint8_t>                  payload;
    std::chrono::system_clock::time_point timestamp = {};
    uint64_t                              seq       = 0;
    std::map<std::string, std::string>    meta;
};

// ── Error codes — mandatory sentinels (§5.1) ─────────────────────────────────

enum class Errc { closed = 0, not_connected = 1, timeout = 2, payload_too_large = 3 };

inline const std::error_category& relay_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "relay"; }
        std::string message(int ev) const override {
            switch (static_cast<Errc>(ev)) {
            case Errc::closed:            return "relay: closed";
            case Errc::not_connected:     return "relay: not connected";
            case Errc::timeout:           return "relay: timeout";
            case Errc::payload_too_large: return "relay: payload too large";
            default:                      return "relay: unknown";
            }
        }
    };
    static Cat inst;
    return inst;
}

inline std::error_condition make_error_condition(Errc e) noexcept {
    return {static_cast<int>(e), relay_category()};
}

// Sentinel error conditions (§5.1). Use error_condition (not error_code) so
// protocol-specific codes can map to these via std::error_condition equivalence.
inline const std::error_condition ErrClosed          = make_error_condition(Errc::closed);
inline const std::error_condition ErrNotConnected    = make_error_condition(Errc::not_connected);
inline const std::error_condition ErrTimeout         = make_error_condition(Errc::timeout);
inline const std::error_condition ErrPayloadTooLarge = make_error_condition(Errc::payload_too_large);

// ── Context (§18.2) ──────────────────────────────────────────────────────────

class Context {
public:
    static Context background() noexcept { return Context{}; }

    static Context with_deadline(std::chrono::steady_clock::time_point tp) noexcept {
        Context c;
        c.deadline_ = tp;
        return c;
    }

    static Context with_timeout(std::chrono::steady_clock::duration d) noexcept {
        return with_deadline(std::chrono::steady_clock::now() + d);
    }

    bool done() const noexcept {
        if (!deadline_) return false;
        return std::chrono::steady_clock::now() >= *deadline_;
    }

    std::optional<std::chrono::steady_clock::time_point> deadline() const noexcept {
        return deadline_;
    }

private:
    std::optional<std::chrono::steady_clock::time_point> deadline_;
};

// ── BackPressurePolicy and SubscriberOptions (§18.2) ─────────────────────────

enum class BackPressurePolicy { drop_newest = 0, drop_oldest = 1, block = 2 };

struct SubscriberOptions {
    std::size_t        channel_depth = 64;
    BackPressurePolicy back_pressure = BackPressurePolicy::drop_newest;
    // topic_name (§14.1, v0.3): DDS adapters route subscriptions to this topic.
    // All other adapters — including RCP — ignore it. Empty = unset.
    std::string        topic_name;
};

// ── Channel<T> — typed bounded channel (§18.2) ───────────────────────────────
//
// push() is safe to call from a single writer thread concurrently with recv().
// Multiple concurrent writers MUST be serialised externally (§18.2 note).
// Returns true if enqueued; false if full or closed.

template<typename T>
class Channel {
public:
    explicit Channel(std::size_t capacity = 64) : cap_(capacity) {}

    bool push(T value) {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_) return false;
        if (queue_.size() >= cap_) return false;
        queue_.push_back(std::move(value));
        cv_.notify_one();
        return true;
    }

    std::optional<T> recv() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt;
        T v = std::move(queue_.front());
        queue_.pop_front();
        return v;
    }

    std::optional<T> try_recv() {
        std::lock_guard<std::mutex> lk(mu_);
        if (queue_.empty()) return std::nullopt;
        T v = std::move(queue_.front());
        queue_.pop_front();
        return v;
    }

    void close() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

    bool is_closed() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return closed_;
    }

private:
    mutable std::mutex      mu_;
    std::condition_variable cv_;
    std::deque<T>           queue_;
    std::size_t             cap_;
    bool                    closed_ = false;
};

// ── Node and Caller abstract classes (§18.2) ─────────────────────────────────
//
// Note: subscribe() returns shared_ptr<Channel<Message>> rather than the spec's
// Channel<Message> by value because Channel contains a non-movable std::mutex.
// See RELAY issue #3. The shared ownership model aligns with cpp-RCP's existing
// StatusChannel usage pattern.

class Node {
public:
    virtual Protocol protocol() const noexcept = 0;
    virtual std::error_code send(Context ctx, const Message& msg) = 0;
    virtual std::pair<std::shared_ptr<Channel<Message>>, std::error_code>
        subscribe(SubscriberOptions opts = {}) = 0;
    virtual std::error_code close() noexcept = 0;
    virtual ~Node() = default;
};

class Caller : public Node {
public:
    virtual std::pair<Message, std::error_code>
        call(Context ctx, const Message& req) = 0;
};

} // namespace relay

// Enable std::error_condition construction from relay::Errc.
namespace std {
template <>
struct is_error_condition_enum<relay::Errc> : true_type {};
} // namespace std
