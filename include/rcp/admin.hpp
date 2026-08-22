// fusa:req REQ-ADMIN-001
// fusa:req REQ-ADMIN-002
// fusa:req REQ-ADMIN-003
// fusa:req REQ-ADMIN-004
// fusa:req REQ-ADMIN-005
// fusa:req REQ-ADMIN-006
// fusa:req REQ-ADMIN-007
// fusa:req REQ-ADMIN-008

// In-process Admin API: stream listing, SSE-style events, Prometheus
// metrics (Phase 17 ground-up rewrite pass, cpp-RCP issue #129 — ROADMAP.md
// Phase 17 §5, "Transport: udp, l2, shmem, admin").
//
// AdminServer is a lightweight in-process interface: callers can query
// which streams are registered (via shmem::Registry — see "Registry
// reference vs. caller-driven" below), subscribe to events (SSE-style push
// channel), and snapshot Prometheus-format text metrics. An actual HTTP
// binding is out of scope; use a libmicrohttpd or Asio adapter to expose
// the HTTP surface.
//
// ── What ported from c-RCP's admin.h/admin.c, and what deliberately didn't ──
// c-RCP's admin.h/admin.c (include/rcp/admin.h, src/admin.c) is this pass's
// content source of truth for two genuine deltas ported below, plus one
// deliberate non-port:
//
// 1. emit() deadlock risk (PORTED). c-RCP's own admin.h header comment
//    (lines ~34-42) flags this by name: "Deviation from cpp-RCP...:
//    cpp-RCP's emit() holds its mutex for the duration of every subscriber
//    callback invocation. This port invokes callbacks outside the lock
//    instead...a subscriber that calls back into the same server...cannot
//    deadlock." That described a real, live bug in this file before this
//    pass: emit() held std::lock_guard<std::mutex> lk(mu_) across the
//    entire `for (auto& cb : subscribers_) cb(ev)` loop, so a subscriber
//    callback that itself called srv.subscribe()/emit()/record_counter()
//    (all of which also lock mu_, a plain non-recursive std::mutex) would
//    deadlock against itself. Fixed below by copying the subscriber list
//    out under the lock, releasing the lock, and only then invoking the
//    callbacks — the same "copy under lock, invoke unlocked" idiom
//    rcp/shmem.hpp's own Channel::request() already established in this
//    codebase's sibling Phase 17 wave (copies handler_ under mu_, invokes
//    the copy outside the lock — see that function's own comment) and
//    c-RCP's admin.c itself uses (a fixed-size local[RCP_ADMIN_MAX_
//    SUBSCRIBERS] stack snapshot, admin.c's rcp_admin_server_emit()).
//    Regression-tested below ("emit: a subscriber that calls back into the
//    same server does not deadlock").
//
// 2. Fixed-capacity subscribers_/counters_ (PORTED). c-RCP's
//    RCP_ADMIN_MAX_SUBSCRIBERS (16) / RCP_ADMIN_MAX_COUNTERS (256) bound
//    admin.c's own subscriber list and counter table — fixed embedded
//    arrays, not heap allocations growable without bound (c-RCP admin.h's
//    own [c-RCP-17] doc comment). Before this pass, this file's
//    subscribers_/counters_ were an unbounded std::vector/
//    std::unordered_map — the same "no fixed-capacity/no-dynamic-
//    allocation architecture" gap ROADMAP.md's Phase 17 introduction names
//    as one motivation for this whole rewrite, and the same shape
//    rcp/watchdog.hpp's Manager::kMaxStreams/kMaxCallbacks (also 16) fixed
//    for this codebase's Phase 14 wave. Ported unchanged (16/256) below as
//    kMaxSubscribers/kMaxCounters, backing std::array storage with a manual
//    length counter — matching rcp/watchdog.hpp's Manager idiom exactly
//    rather than introducing yet another generic bounded-container
//    abstraction (rcp/request.hpp's detail::BoundedVector<T,N> is
//    deliberately scoped private to that file — this codebase's existing
//    convention is each module rolling its own fixed std::array + _len_
//    pair, not sharing one container type across modules; see
//    rcp/alloc.hpp's header comment for the same "each stateful primitive
//    is its own small instance-owned thing" preference stated explicitly).
//    subscribe()/record_counter() now return std::error_code
//    (AdminErrc::subscriber_capacity_exceeded/counter_capacity_exceeded)
//    instead of silently succeeding forever, mirroring
//    rcp::watchdog::Manager::subscribe()'s identical return-shape
//    precedent. Boundary tests ported below: "subscribe: at max succeeds,
//    next fails" / "record_counter: at max succeeds, a repeat of an
//    existing counter still succeeds, a genuinely new one fails" — the
//    cpp-RCP equivalents of c-RCP's own
//    test_subscribe_at_max_succeeds_then_next_fails/
//    test_record_counter_at_max_succeeds_then_next_new_one_fails
//    (tests/test_admin.c).
//
//    RCP_ADMIN_MAX_ENDPOINTS (64) deliberately does NOT transfer: it bounds
//    c-RCP's own endpoints[] array, which exists only because c-RCP's
//    admin.c tracks its registered-endpoint set itself (see point 3 below
//    for why). This file's streams() has never tracked its own stream set
//    — it reads live off shmem::Registry (rcp/shmem.hpp), whose own
//    capacity (currently unbounded, a std::map — see that file's own
//    Registry class comment, unchanged by this pass) is that file's
//    decision to make, not this one's.
//
// 3. Registry reference vs. c-RCP's caller-driven register_endpoint()/
//    deregister_endpoint() (NOT PORTED — deliberate). c-RCP's own admin.h
//    header comment (lines ~10-30) explains why ITS admin module dropped
//    its own registry dependency entirely: c-RCP's admin.c used to wrap
//    rcp.h's rcp_registry_t and enumerate its rcp_controller_t instances,
//    but "rcp_registry_t has no TC18 counterpart -- there is no single
//    generic registry of 'every controller' left to introspect" once that
//    retired core was replaced, so c-RCP's admin.c became caller-driven
//    instead: whatever code discovers or configures endpoints tells
//    admin.c directly via rcp_admin_server_register_endpoint()/
//    _deregister_endpoint() (which just insert/remove a caller-supplied
//    rcp_avtp_addr_t into admin.c's own fixed endpoints[] array — see
//    point 2 above), and rcp_admin_server_endpoints() reports back exactly
//    that caller-maintained membership.
//
//    That rationale does not apply here, and porting it anyway would
//    actively regress this codebase: unlike c-RCP's rcp_registry_t,
//    rcp::shmem::Registry is NOT retired — it is a live, current, still-
//    actively-maintained concept (rebuilt in this exact same Phase 17 wave,
//    the sibling `phase5/shmem` branch this branch is based on), with a
//    real keyed add_channel()/deregister()/lookup()/channels()/close()
//    surface AdminServer::streams() already depends on. rcp/shmem.hpp's own
//    header comment (its "Registry" class comment) explicitly commits to
//    this: "rcp::admin::AdminServer (rcp/admin.hpp) ... only ever depend[s]
//    on Registry's keyed add/lookup/enumerate surface ... never on
//    Channel::request()'s internals, and that surface is unchanged here —
//    verified by reading both files and their tests ... in full; neither
//    calls Channel::request() at all." Migrating streams() to a caller-
//    driven register_endpoint()/deregister_endpoint() pair would make
//    AdminServer track a second, independent copy of "what streams exist"
//    that callers would have to remember to keep in sync with Registry by
//    hand (register on add_channel(), deregister on deregister()) —
//    reintroducing exactly the dual-source-of-truth bug class a registry-
//    reference design avoids by construction, for no offsetting benefit,
//    since (unlike c-RCP) there is still a live, single, authoritative
//    registry object to read from directly. Also verified this is not
//    disproportionately load-bearing to keep either way: grepped this tree
//    codebase-wide for AdminServer/rcp::admin/admin:: — no caller other
//    than tests/test_admin.cpp constructs an AdminServer at all
//    (rcp/config.hpp, in particular, has never wired one). Decision:
//    AdminServer keeps its shmem::Registry& constructor reference and
//    streams() keeps reading reg_.channels() live; StreamInfo/EventType/
//    Event/stream_key naming is otherwise unchanged from this file's
//    pre-Phase-17 (v2.19.0) shape.
//
// Every other REQ-ADMIN-* citation and behavior (streams()/subscribe()/
// emit()/record_counter()/metrics_text()) is unchanged in shape from the
// v2.19.0 rebind — see git history for that pass's own rationale
// (ROADMAP.md's "Retired-model residue cleanup" milestone).
#pragma once

#include "rcp.hpp"
#include "shmem.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace admin {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class AdminErrc : int {
    // [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] Ported from
    // c-RCP's RCP_ADMIN_MAX_SUBSCRIBERS/RCP_ADMIN_MAX_COUNTERS fixed-
    // capacity bound (admin.h) — see AdminServer::kMaxSubscribers/
    // kMaxCounters's own doc comment below and this file's header comment,
    // delta #2.
    subscriber_capacity_exceeded = 1, // AdminServer already holds kMaxSubscribers subscribers
    counter_capacity_exceeded    = 2, // AdminServer already tracks kMaxCounters distinct (name, labels) counters
};

inline const std::error_category& admin_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.admin"; }
        std::string message(int ev) const override {
            switch (static_cast<AdminErrc>(ev)) {
            case AdminErrc::subscriber_capacity_exceeded:
                return "rcp/admin: AdminServer already holds its fixed maximum number of subscribers";
            case AdminErrc::counter_capacity_exceeded:
                return "rcp/admin: AdminServer already tracks its fixed maximum number of counters";
            default:
                return "rcp/admin: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AdminErrc e) noexcept {
    return {static_cast<int>(e), admin_category()};
}

// ── StreamInfo ────────────────────────────────────────────────────────────────

struct StreamInfo {
    uint64_t    stream_key;
    bool        registered;
    std::string extra; // JSON-encoded metadata blob, optional
};

// ── Event ─────────────────────────────────────────────────────────────────────

enum class EventType : uint8_t { StreamRegistered = 1, StreamDeregistered = 2, StatusUpdate = 3 };

struct Event {
    EventType                             type;
    uint64_t                              stream_key;
    std::chrono::system_clock::time_point ts;
};

using EventCallback = std::function<void(const Event&)>;

// ── Counter ───────────────────────────────────────────────────────────────────

struct Counter {
    std::string name;
    std::string labels;
    double      value{0.0};
};

// ── AdminServer ───────────────────────────────────────────────────────────────

class AdminServer {
public:
    // [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] Ported from
    // c-RCP's include/rcp/admin.h RCP_ADMIN_MAX_SUBSCRIBERS/
    // RCP_ADMIN_MAX_COUNTERS (16/256 respectively) — see this file's header
    // comment, delta #2, for the full rationale and for why
    // RCP_ADMIN_MAX_ENDPOINTS does NOT have a counterpart here. Backs the
    // fixed std::array storage below (subscribers_, counters_), not
    // std::vector/std::unordered_map growable without bound.
    static constexpr size_t kMaxSubscribers = 16;
    static constexpr size_t kMaxCounters    = 256;

    explicit AdminServer(shmem::Registry& reg) : reg_(reg) {}

    // streams returns a snapshot of all registered streams — read live off
    // reg_ (shmem::Registry), not a copy AdminServer maintains itself; see
    // this file's header comment, delta #3.
    std::vector<StreamInfo> streams() const {
        auto                     channels = reg_.channels();
        std::vector<StreamInfo> out;
        out.reserve(channels.size());
        for (auto& ch : channels) {
            out.push_back({ch->stream_key(), true, {}});
        }
        return out;
    }

    // subscribe registers cb to be invoked (in registration order) on every
    // subsequent emit() call. Returns AdminErrc::subscriber_capacity_
    // exceeded, unchanged, once kMaxSubscribers subscribers are already
    // registered — see kMaxSubscribers's own doc comment above.
    std::error_code subscribe(EventCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        if (subscribers_len_ >= kMaxSubscribers) return make_error_code(AdminErrc::subscriber_capacity_exceeded);
        subscribers_[subscribers_len_] = std::move(cb);
        ++subscribers_len_;
        return {};
    }

    // emit broadcasts ev to every registered subscriber, in registration
    // order. The subscriber list is copied out under mu_ and every callback
    // is invoked AFTER mu_ is released — see this file's header comment,
    // delta #1: a subscriber that calls back into this same AdminServer
    // (subscribe()/emit()/record_counter()) cannot deadlock.
    void emit(Event ev) {
        std::array<EventCallback, kMaxSubscribers> local;
        size_t                                     n;
        {
            std::lock_guard<std::mutex> lk(mu_);
            n = subscribers_len_;
            for (size_t i = 0; i < n; ++i) local[i] = subscribers_[i];
        }
        // Invoked outside the lock — see the deviation note above.
        for (size_t i = 0; i < n; ++i) local[i](ev);
    }

    // record_counter adds delta to the running total of the counter
    // identified by (name, labels) — a distinct running total is kept per
    // unique (name, labels) pair. Returns AdminErrc::counter_capacity_
    // exceeded without recording delta if (name, labels) is not already
    // tracked and this AdminServer already holds kMaxCounters distinct
    // counters; an already-tracked (name, labels) pair always succeeds
    // regardless of how many other counters exist — see kMaxCounters's own
    // doc comment above.
    std::error_code record_counter(const std::string& name, const std::string& labels, double delta) {
        std::lock_guard<std::mutex> lk(mu_);
        for (size_t i = 0; i < counters_len_; ++i) {
            if (counters_[i].name == name && counters_[i].labels == labels) {
                counters_[i].value += delta;
                return {};
            }
        }
        if (counters_len_ >= kMaxCounters) return make_error_code(AdminErrc::counter_capacity_exceeded);
        counters_[counters_len_] = Counter{name, labels, delta};
        ++counters_len_;
        return {};
    }

    // metrics_text returns Prometheus text-format metric lines.
    std::string metrics_text() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::ostringstream           oss;
        for (size_t i = 0; i < counters_len_; ++i) {
            oss << "# TYPE " << counters_[i].name << " counter\n";
            oss << counters_[i].name;
            if (!counters_[i].labels.empty()) oss << "{" << counters_[i].labels << "}";
            oss << " " << counters_[i].value << "\n";
        }
        return oss.str();
    }

    // Introspection for tests, not part of the subscribe()/record_counter()
    // contract itself — always <= kMaxSubscribers/kMaxCounters.
    size_t subscriber_count() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return subscribers_len_;
    }
    size_t counter_count() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return counters_len_;
    }

private:
    shmem::Registry&    reg_;
    mutable std::mutex  mu_; // protects subscribers_/subscribers_len_/counters_/counters_len_
    std::array<EventCallback, kMaxSubscribers> subscribers_{};
    size_t                                     subscribers_len_ = 0; // always <= kMaxSubscribers
    std::array<Counter, kMaxCounters>          counters_{};
    size_t                                     counters_len_ = 0; // always <= kMaxCounters
};

} // namespace admin
} // namespace rcp

// Enable std::error_code construction from rcp::admin::AdminErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::admin::AdminErrc> : true_type {};
} // namespace std
