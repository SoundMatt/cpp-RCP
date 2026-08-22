// fusa:req REQ-SHMEM-001
// fusa:req REQ-SHMEM-002
// fusa:req REQ-SHMEM-003
// fusa:req REQ-SHMEM-004
// fusa:req REQ-SHMEM-005
// fusa:req REQ-SHMEM-006
// fusa:req REQ-SHMEM-007
// fusa:req REQ-SHMEM-008
// fusa:req REQ-SHMEM-009
// fusa:req REQ-SHMEM-010

// In-process, bounded-queue request delivery (cpp-RCP issue #129 Phase 5
// wave 2; ROADMAP.md Phase 17 v3.0.0 rewrite).
//
// This is the in-process analog c-RCP's own shmem.h/shmem.c (99/266 lines)
// plays for a real byte-level transport: like c-RCP's
// rcp_shmem_avtp_pair_new(), this header moves real, encoded ACF_ABB/
// ACF_GBB bytes (rcp/acf.hpp) through a fixed-capacity, in-process buffer
// and backpressures with a real error (ErrBusy) once that capacity is
// exhausted, rather than growing without bound or silently dropping
// anything. "shmem" is c-RCP's own name for this concept, not a claim of
// real OS shared memory: neither codebase has ever `shm_open()`/`mmap()`ed
// anything here — c-RCP's shmem.c uses plain heap allocation and a
// non-PTHREAD_PROCESS_SHARED mutex/condvar (rcp_mutex_t/rcp_cond_t), and
// this header uses std::mutex/std::shared_mutex — both in-process only, no
// real I/O, exactly as c-RCP's own shmem.h header comment (lines 12-22)
// already says of itself ("entirely in-process (no real I/O)"). An earlier
// revision of this header instead described Channel/Registry as a
// "zero-copy" pass-through with "no bytes are ever serialised" — that
// description is now wrong twice over: it implied a capability (avoiding
// serialisation overhead) as if it were this module's distinguishing value
// versus a real transport, and the implementation genuinely had no
// capacity bound of any kind (Channel::request() called a handler directly
// with no buffer between caller and callee at all) — the two real gaps
// this pass fixes, both ported from c-RCP's actual shape rather than
// re-derived:
//   1. No capacity bound existed. c-RCP's queue_capacity constructor
//      parameter (shmem.h:91, clamped to a minimum of 1 at shmem.c:204) and
//      its RCP_ERR_BUSY-on-full send() (shmem.c:68-71, REQ-SHMEM-006) are
//      ported below as Channel's own queue_capacity constructor parameter
//      and ErrBusy-on-full request() (REQ-SHMEM-006 again — see that
//      section below for why this pass corrects two REQ-SHMEM-006/007
//      mis-citations found in c-RCP's own shmem.c while porting this).
//   2. The ACF codec was never exercised on this path at all — a request
//      reached its handler as the exact same in-memory acf::AcfMessageInfo/
//      std::vector<uint8_t> objects the caller built, never encoded to or
//      decoded from bytes. Channel::request() below now encodes the
//      outbound request via acf::encode_acf_abb()/encode_acf_gbb() (info.
//      acf_msg_type selects which, the same selection rcp/udp.hpp's
//      encode_frame()/decode_frame() make at udp.hpp:209-232/234-278 —
//      reused here as this header's own idiom reference rather than
//      re-derived) before it ever reaches the handler, and decodes the
//      handler's response the same way before returning it to the caller.
//
// Handler's own signature is UNCHANGED: it is still handed fully decoded
// acf::AcfMessageInfo/std::vector<uint8_t> objects, exactly as udp::
// Server's own Handler is (udp.hpp:424-427) despite udp::Server doing real
// byte encode/decode internally around it (udp.hpp:209-278) — the same
// "real codec inside, decoded objects at the boundary" split this header
// now follows too. rcp::admin::AdminServer (rcp/admin.hpp) and rcp::config
// ::load() (rcp/config.hpp) only ever depend on Registry's keyed add/
// lookup/enumerate surface (add_channel/deregister/lookup/channels/close),
// never on Channel::request()'s internals, and that surface is unchanged
// here — verified by reading both files and their tests (tests/
// test_admin.cpp, tests/test_config.cpp) in full; neither calls
// Channel::request() at all.
//
// This header has no clock or socket of its own, same as before — Channel::
// request() still dispatches synchronously on the calling thread, the
// "primitives driven by the embedding application" convention every Phase
// 14/15 header has used since v2.9.0. Unlike c-RCP's shmem.c, it also still
// has no thread of its own: c-RCP's pair is genuinely asynchronous (two
// independent rcp_avtp_transport_t sides, each send()ing/recv()ing from
// its own, possibly different, thread, correlated only by strict FIFO
// order — shmem.c's ring_push()/shmem_side_recv()); Channel::request()
// plays both roles — the party that encodes+enqueues AND the party that
// dequeues+decodes+dispatches — inside one synchronous call on one thread,
// so there is no second, independent consumer a FIFO handoff would ever be
// needed to correlate with. Channel's own bounded buffer (detail::
// FrameSlots below) is therefore a fixed-capacity POOL of indexed slots a
// caller acquires-and-later-releases, not a literal head/tail FIFO ring —
// see FrameSlots's own comment for the full reasoning and for exactly which
// property of c-RCP's ring this still preserves (a caller's own bytes are
// never handed to, or released by, a different caller) versus which it
// cannot (strict FIFO order across independent readers/writers has no
// meaning when there is only ever one party doing both jobs). The capacity
// BOUND and the BUSY-without-enqueuing failure mode — the two things
// REQ-SHMEM-006 actually requires — are preserved exactly.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::ErrClosed/ErrBusy/ErrNotFound/ErrAlreadyExists only — see this header's own scope note above

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace rcp {
namespace shmem {

// ── Bounded frame codec + slot pool ──────────────────────────────────────────

namespace detail {

// encode_acf_message/decode_acf_message — Channel's own internal ACF codec
// step (this file's header comment, gap #2). Selects ACF_ABB vs ACF_GBB by
// info.acf_msg_type, mirroring rcp/udp.hpp's encode_frame()/decode_frame()
// selection (udp.hpp:209-232/234-278) rather than re-deriving it. ACF_GBB's
// message_timestamp is always sent, and always decoded back, as 0: Handler's
// own decoded-object contract (like rcp::mock::Server::dispatch's, which
// rcp/udp.hpp's own Handler comment cites as its own shape reference) has no
// parameter slot to carry a real timestamp value through to or from the
// handler, so a genuine one would be silently discarded before ever
// reaching it regardless — a known, documented scope limit, not a
// round-trip bug in encode_acf_gbb()/decode_acf_gbb() themselves (both are
// exercised bit-for-bit elsewhere, tests/test_acf.cpp).
inline std::vector<uint8_t> encode_acf_message(const acf::AcfMessageInfo& info,
                                                const std::vector<uint8_t>& payload) {
    return (info.acf_msg_type == acf::kAcfMsgTypeGbb)
               ? acf::encode_acf_gbb(info, /*message_timestamp=*/0, payload)
               : acf::encode_acf_abb(info, payload);
}

inline std::error_code decode_acf_message(const std::vector<uint8_t>& bytes,
                                           acf::AcfMessageInfo& out_info,
                                           std::vector<uint8_t>& out_payload) {
    if (bytes.empty()) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (acf::peek_acf_msg_type(bytes.data()) == acf::kAcfMsgTypeGbb) {
        uint64_t discarded_timestamp = 0;
        return acf::decode_acf_gbb(bytes.data(), bytes.size(), out_info, discarded_timestamp,
                                    out_payload);
    }
    return acf::decode_acf_abb(bytes.data(), bytes.size(), out_info, out_payload);
}

// FrameSlots<Capacity> — a small, fixed-capacity pool of encoded-ACF-frame
// byte buffers backing Channel::request()'s bounded round trip (REQ-SHMEM-
// 006; c-RCP's shmem.c ring_push()/shmem_side_send()/shmem_side_recv(),
// lines 40-45/53-76/84-131). Physically fixed at compile time
// (std::array<Capacity>, no heap growth of the pool itself — the same
// "vector-shaped ergonomics, no unbounded growth" convention rcp/
// request.hpp's BoundedVector<T,N> already establishes for this codebase),
// with a runtime-checked logical capacity <= Capacity so a Channel can
// still be constructed with a caller-chosen queue_capacity — the same role
// rcp_shmem_avtp_pair_new()'s own queue_capacity parameter plays
// (shmem.h:91-93) — without Channel itself needing to become a template.
//
// Departs from shmem.c's own strict head/tail FIFO ring shape in one
// respect, for a reason specific to this header (see the file header
// comment above for the full explanation): a caller acquire()s and later
// release()s exactly one slot of its own, addressed by the index acquire()
// returns, rather than handing a frame to an independent consumer that
// drains strictly in arrival order — there is no independent consumer here
// for a FIFO to correlate with. What carries over unchanged is the thing
// REQ-SHMEM-006 actually requires: a hard capacity bound, and a BUSY
// result — with the pool left completely untouched — the instant that
// bound is reached.
template <std::size_t Capacity>
class FrameSlots {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    // Clamped into [1, Capacity], the same way c-RCP's own
    // rcp_shmem_avtp_pair_new() clamps a caller's queue_capacity==0 up to 1
    // (shmem.c:204) — a 0-slot pool would make every acquire() call
    // unconditionally BUSY, never a reachable, useful configuration.
    void set_logical_capacity(size_t n) noexcept {
        logical_capacity_ = n < 1 ? 1 : (n > Capacity ? Capacity : n);
    }
    size_t logical_capacity() const noexcept { return logical_capacity_; }

    // acquire finds a free slot, stores `frame` into it, and returns its
    // index — or npos, leaving every slot untouched, once
    // logical_capacity() slots are already occupied (REQ-SHMEM-006: BUSY
    // without enqueuing).
    size_t acquire(std::vector<uint8_t> frame) {
        for (size_t i = 0; i < logical_capacity_; ++i) {
            if (!occupied_[i]) {
                slots_[i]    = std::move(frame);
                occupied_[i] = true;
                return i;
            }
        }
        return npos;
    }

    const std::vector<uint8_t>& at(size_t idx) const noexcept { return slots_[idx]; }
    void store(size_t idx, std::vector<uint8_t> frame) { slots_[idx] = std::move(frame); }

    void release(size_t idx) noexcept {
        slots_[idx].clear();
        slots_[idx].shrink_to_fit();
        occupied_[idx] = false;
    }

    size_t occupied_count() const noexcept {
        size_t n = 0;
        for (size_t i = 0; i < logical_capacity_; ++i) {
            if (occupied_[i]) ++n;
        }
        return n;
    }

private:
    std::array<std::vector<uint8_t>, Capacity> slots_{};
    std::array<bool, Capacity>                 occupied_{};
    size_t                                      logical_capacity_ = Capacity;
};

} // namespace detail

// kMaxQueueCapacity: the physical, compile-time ceiling every Channel's
// FrameSlots pool is sized to, regardless of the queue_capacity a caller
// requests — the same order of magnitude as other c-RCP-17 fixed-capacity
// conversions elsewhere in this project's history (e.g.
// RCP_POWERSTATE_MAX_ENDPOINTS=64, rcp/request.hpp's kMaxTrackedRequests=
// 64). kDefaultQueueCapacity is what Channel/new_channel() use when a
// caller doesn't pass one explicitly — small enough that a test can
// exhaust it deliberately (see tests/test_shmem.cpp's backpressure cases).
constexpr size_t kMaxQueueCapacity     = 32;
constexpr size_t kDefaultQueueCapacity = 8;

// ── Channel ───────────────────────────────────────────────────────────────────

// Channel is the in-process, bounded-queue analog of rcp/udp.hpp's Server:
// a caller-supplied Handler answers each request, in the same process, with
// a real byte encode/decode step — and a real, boundable capacity — between
// caller and handler (this file's header comment). Handler is shaped
// identically to udp::Server::Handler's own decoded-object contract so the
// same handler (e.g. rcp::mock::Server::dispatch) can be wired to either
// transport.
class Channel {
public:
    using Handler = std::function<std::error_code(size_t client,
                                                    const acf::AcfMessageInfo& req,
                                                    const std::vector<uint8_t>& req_payload,
                                                    acf::AcfMessageInfo& out_resp,
                                                    std::vector<uint8_t>& out_resp_payload)>;

    // queue_capacity bounds how many request() calls may have their own
    // slot concurrently occupied — i.e. be "in flight" through this Channel
    // — before a new caller is rejected with ErrBusy instead of being
    // admitted (REQ-SHMEM-006). Clamped into [1, kMaxQueueCapacity] by
    // FrameSlots::set_logical_capacity() above.
    explicit Channel(uint64_t stream_key, size_t queue_capacity = kDefaultQueueCapacity)
        : stream_key_(stream_key) {
        slots_.set_logical_capacity(queue_capacity);
    }

    uint64_t stream_key() const noexcept { return stream_key_; }

    // queue_capacity/queue_depth expose the bound this pass adds and its
    // current occupancy — c-RCP has no equivalent accessor (shmem.c's own
    // queue depth is private to its ring), but both are useful for a caller
    // (or a test) that wants to observe backpressure building up rather
    // than only discovering it via a returned ErrBusy.
    size_t queue_capacity() const noexcept { return slots_.logical_capacity(); }
    size_t queue_depth() const {
        std::lock_guard<std::mutex> lk(slots_mu_);
        return slots_.occupied_count();
    }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
    }

    // request encodes `req`/`req_payload` to bytes, admits them into this
    // Channel's bounded slot pool (ErrBusy — REQ-SHMEM-006 — if every slot
    // is already occupied by another in-flight request()), decodes them
    // back out, and delivers the decoded result to the registered Handler
    // — then encodes and decodes the Handler's own response the same way
    // before returning it, so a caller's out_resp/out_resp_payload reflect
    // exactly what a real byte-level transport would have delivered rather
    // than the handler's in-memory objects verbatim. If no Handler is set,
    // the default response is ResponseKind::Acknowledge — the same "answer
    // something, don't hang" default rcp/udp.hpp's Server uses when
    // unhandled.
    std::error_code request(size_t client, const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload) {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed; // REQ-SHMEM-005/010

        size_t slot;
        {
            std::lock_guard<std::mutex> lk(slots_mu_);
            slot = slots_.acquire(detail::encode_acf_message(req, req_payload));
        }
        if (slot == detail::FrameSlots<kMaxQueueCapacity>::npos) return ErrBusy; // REQ-SHMEM-006

        // Releases this call's own slot on every return path below,
        // including an early return from a decode failure or a Handler
        // error — mirrors shmem_side_destroy()'s own "always releases what
        // it holds" contract (shmem.c:151-181, REQ-SHMEM-008/009 — see
        // Channel's own destructor comment near the bottom of this class
        // for why those two are otherwise automatic here).
        struct SlotGuard {
            Channel* ch;
            size_t   idx;
            ~SlotGuard() {
                std::lock_guard<std::mutex> lk(ch->slots_mu_);
                ch->slots_.release(idx);
            }
        } guard{this, slot};

        // Decode back what was just admitted — proves the decode half of
        // the codec round trip actually runs on the fast path too, not
        // just encode (this file's header comment, gap #2).
        acf::AcfMessageInfo  decoded_req;
        std::vector<uint8_t> decoded_payload;
        {
            std::lock_guard<std::mutex> lk(slots_mu_);
            if (auto ec = detail::decode_acf_message(slots_.at(slot), decoded_req, decoded_payload))
                return ec;
        }

        // Copy the Handler under mu_ and invoke the copy outside the lock:
        // mu_ only ever needs to protect handler_ itself (set_handler() may
        // race with request() on another thread) — holding it across the
        // handler CALL as well would serialize every concurrent request()
        // through this one Channel to strictly one-at-a-time, silently
        // defeating the whole point of a >1 queue_capacity (REQ-SHMEM-006
        // exists to bound genuinely concurrent in-flight requests, not to
        // manufacture ones that no longer overlap).
        Handler handler_copy;
        {
            std::lock_guard<std::mutex> lk(mu_);
            handler_copy = handler_;
        }

        std::error_code ec;
        if (handler_copy) {
            ec = handler_copy(client, decoded_req, decoded_payload, out_resp, out_resp_payload);
        } else {
            out_resp = acf::make_response(decoded_req, acf::ResponseKind::Acknowledge);
            out_resp_payload.clear();
        }
        if (ec) return ec;

        auto resp_bytes = detail::encode_acf_message(out_resp, out_resp_payload);
        {
            std::lock_guard<std::mutex> lk(slots_mu_);
            slots_.store(slot, resp_bytes);
        }
        return detail::decode_acf_message(resp_bytes, out_resp, out_resp_payload);
    }

    // close is idempotent — safe to call more than once.
    void close() { closed_.store(true, std::memory_order_release); }

    bool ok() const noexcept { return !closed_.load(std::memory_order_acquire); }

    // Channel has no manual refcount/destroy step the way c-RCP's
    // shmem_side_destroy() does (shmem.c:151-181): it is always held via
    // std::shared_ptr<Channel> (new_channel() below, Registry's own
    // storage), whose atomic refcounting already gives "freed exactly once,
    // regardless of release order" (REQ-SHMEM-009) and "releasing one
    // holder does not itself invalidate another holder's own use of the
    // same Channel" (REQ-SHMEM-008) for free — the same "destructors, RAII
    // covers this by construction" convention rcp/watchdog.hpp's own header
    // comment documents for an analogous case. Neither REQ needs, or gets,
    // any bespoke code here.

private:
    uint64_t            stream_key_;
    std::atomic<bool>   closed_{false};
    mutable std::mutex  mu_;       // guards handler_
    mutable std::mutex  slots_mu_; // guards slots_
    detail::FrameSlots<kMaxQueueCapacity> slots_;
    Handler             handler_;
};

// ── Registry ──────────────────────────────────────────────────────────────────

// Registry looks up a Channel by its stream_key, the same opaque
// uint64_t-keyed lookup role rcp/watchdog.hpp's Manager and
// rcp/regmap.hpp's Ep0 already establish for this codebase (typically an
// avtp::StreamId::to_u64()). Unchanged by this pass — c-RCP has no analog
// of Registry at all (rcp_shmem_avtp_pair_new() returns exactly one pair,
// with no keyed lookup of any kind), and rcp::admin::AdminServer (rcp/
// admin.hpp) and rcp::config::load() (rcp/config.hpp) only ever depend on
// this exact surface (add_channel/deregister/lookup/channels/close) — see
// this file's own header comment.
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

inline std::shared_ptr<Channel> new_channel(uint64_t stream_key,
                                             size_t   queue_capacity = kDefaultQueueCapacity) {
    return std::make_shared<Channel>(stream_key, queue_capacity);
}

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

} // namespace shmem
} // namespace rcp
