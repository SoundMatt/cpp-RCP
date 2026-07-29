// fusa:req REQ-CAPI-001
// fusa:req REQ-CAPI-002
// fusa:req REQ-CAPI-003
// fusa:req REQ-CAPI-004
// fusa:req REQ-CAPI-005
// fusa:req REQ-CAPI-006
// fusa:req REQ-CAPI-007
// fusa:req REQ-CAPI-008
// fusa:req REQ-CAPI-009

// C API implementation — do not include from C code.
//
// Link this translation unit (or include it from exactly one .cpp) to
// satisfy the C API symbols declared in capi.h. All storage is placement-new
// into caller-supplied buffers; there is no dynamic allocation.
//
// ROADMAP.md milestone 60, "C ABI & CLI Rebuild (v2.16.0)": this file
// REPLACES its pre-replacement content in full, per the Satellite Package
// Disposition table's entry for `capi.h`/`capi_impl.hpp`. The pre-
// replacement version bridged rcp_registry_s/rcp_ctrl_s directly to
// rcp::legacy_mock::Registry/Controller (rcp.hpp's Zone/Command/Controller
// model); this version has no dependency on rcp.hpp, rcp/legacy_mock.hpp,
// or any other backend at all — an rcp_ctrl_s is just a bound
// (rcp_request_fn_t, userdata, stream_key, byte_bus_id) tuple, and
// rcp_registry_s is a small fixed-capacity table of such handles. Grepping
// the tree for consumers of the pre-v2.16.0 capi_impl.hpp API beyond its
// own (now-rewritten) test found none, so no legacy-shim split file was
// needed here, same as rcp/udp.hpp's v2.13.0 rebuild and the fourteen files
// ADAPTed at v2.14.0/v2.15.0.
//
// Because rcp_request_fn_t is a plain function pointer with the same
// "client-side send-equivalent call" shape as rcp/record.hpp's/
// rcp/observe.hpp's C++-level RequestFn, this header can be — but is not
// required to be — backed by rcp::mock::Server::dispatch (v2.12.0) or
// rcp::udp::Client::request (v2.13.0) via a small caller-written adapter
// function; see tests/test_capi.cpp for worked examples of both.
#pragma once

#include "capi.h"

#include <cstddef>
#include <new>

// ── Backing storage ──────────────────────────────────────────────────────────

struct rcp_ctrl_s {
    rcp_stream_key_t  stream_key;
    rcp_byte_bus_id_t byte_bus_id;
    rcp_request_fn_t  fn;
    void*             userdata;
};

namespace rcp {
namespace capi {
namespace detail {
// Fixed registry capacity — chosen so rcp_registry_s comfortably fits the
// same "small caller-supplied buffer" size class the pre-replacement
// registry used (<= 512 bytes), with room to spare for future growth.
constexpr std::size_t kMaxRegistryEntries = 16;
} // namespace detail
} // namespace capi
} // namespace rcp

struct rcp_registry_s {
    rcp_ctrl_h  entries[rcp::capi::detail::kMaxRegistryEntries] = {};
    std::size_t count  = 0;
    bool        closed = false;
};

static_assert(sizeof(rcp_registry_s) <= 512, "rcp_registry_s too large");
static_assert(sizeof(rcp_ctrl_s)     <= 64,  "rcp_ctrl_s too large");

// ── Registry ──────────────────────────────────────────────────────────────────

inline rcp_err_t rcp_registry_init(void* buf, size_t buf_len, rcp_registry_h* out) {
    if (!buf || !out) return RCP_ERR_INVALID;
    if (buf_len < sizeof(rcp_registry_s)) return RCP_ERR_NOMEM;
    *out = new (buf) rcp_registry_s{};
    return RCP_OK;
}

inline size_t rcp_registry_sizeof() { return sizeof(rcp_registry_s); }

inline rcp_err_t rcp_registry_close(rcp_registry_h reg) {
    if (!reg) return RCP_ERR_INVALID;
    reg->closed = true;
    reg->~rcp_registry_s();
    return RCP_OK;
}

// ── Controller ────────────────────────────────────────────────────────────────

inline rcp_err_t rcp_ctrl_init(rcp_stream_key_t  stream_key,
                                rcp_byte_bus_id_t byte_bus_id,
                                rcp_request_fn_t  fn,
                                void*             userdata,
                                void*             buf,
                                size_t            buf_len,
                                rcp_ctrl_h*       out) {
    if (!buf || !out) return RCP_ERR_INVALID;
    if (!fn) return RCP_ERR_INVALID;
    if (buf_len < sizeof(rcp_ctrl_s)) return RCP_ERR_NOMEM;
    auto* h        = new (buf) rcp_ctrl_s{};
    h->stream_key   = stream_key;
    h->byte_bus_id  = byte_bus_id;
    h->fn           = fn;
    h->userdata     = userdata;
    *out = h;
    return RCP_OK;
}

inline size_t rcp_ctrl_sizeof() { return sizeof(rcp_ctrl_s); }

inline rcp_err_t rcp_registry_add(rcp_registry_h reg, rcp_ctrl_h ctrl) {
    if (!reg || !ctrl) return RCP_ERR_INVALID;
    if (reg->closed) return RCP_ERR_CLOSED;
    for (std::size_t i = 0; i < reg->count; ++i) {
        if (reg->entries[i]->stream_key == ctrl->stream_key &&
            reg->entries[i]->byte_bus_id == ctrl->byte_bus_id) {
            return RCP_ERR_BUSY; // already registered for this (stream_key, byte_bus_id)
        }
    }
    if (reg->count >= rcp::capi::detail::kMaxRegistryEntries) return RCP_ERR_NOMEM;
    reg->entries[reg->count++] = ctrl;
    return RCP_OK;
}

inline rcp_err_t rcp_registry_lookup(rcp_registry_h    reg,
                                      rcp_stream_key_t  stream_key,
                                      rcp_byte_bus_id_t byte_bus_id,
                                      rcp_ctrl_h*       out) {
    if (!reg || !out) return RCP_ERR_INVALID;
    if (reg->closed) return RCP_ERR_CLOSED;
    for (std::size_t i = 0; i < reg->count; ++i) {
        if (reg->entries[i]->stream_key == stream_key &&
            reg->entries[i]->byte_bus_id == byte_bus_id) {
            *out = reg->entries[i];
            return RCP_OK;
        }
    }
    return RCP_ERR_NOT_FOUND;
}

// ── Send ─────────────────────────────────────────────────────────────────────

inline rcp_err_t rcp_send(rcp_ctrl_h            ctrl,
                           const rcp_request_t*  req,
                           rcp_response_t*       resp,
                           uint32_t              timeout_ms) {
    if (!ctrl || !req || !resp) return RCP_ERR_INVALID;
    if (!resp->payload && resp->payload_cap > 0) return RCP_ERR_INVALID;

    rcp_err_t ec = ctrl->fn(ctrl->userdata, req, resp, timeout_ms);
    if (ec != RCP_OK) return ec;

    // Defensive net around the callback's own contract (capi.h's own
    // header comment): a callback that overruns the caller-supplied output
    // buffer is a callback bug, not something rcp_send() can silently pass
    // through as success.
    if (resp->payload_len > resp->payload_cap) return RCP_ERR_INVALID;
    return RCP_OK;
}
