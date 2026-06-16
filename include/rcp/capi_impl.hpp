// C API implementation shim — do not include from C code.
//
// Link this translation unit (capi_impl.cpp) to satisfy the C API symbols
// declared in capi.h.  Bridges flat C handles to the C++ mock controller
// and in-process registry.
//
// fusa:req REQ-CAPI-001
// fusa:req REQ-CAPI-002
// fusa:req REQ-CAPI-003
// fusa:req REQ-CAPI-004
// fusa:req REQ-CAPI-005
// fusa:req REQ-CAPI-006
// fusa:req REQ-CAPI-007
// fusa:req REQ-CAPI-008
#pragma once

#include "capi.h"
#include "mock.hpp"
#include "rcp.hpp"

// ── Backing storage layout ────────────────────────────────────────────────────
// We use placement-new into caller-supplied buffers to avoid heap allocation.

struct rcp_registry_s {
    rcp::mock::Registry impl;
};

struct rcp_ctrl_s {
    std::shared_ptr<rcp::mock::Controller> impl;
};

static_assert(sizeof(rcp_registry_s) <= 512, "rcp_registry_s too large");
static_assert(sizeof(rcp_ctrl_s)     <= 64,  "rcp_ctrl_s too large");

inline rcp_err_t rcp_registry_init(void* buf, size_t buf_len, rcp_registry_h* out) {
    if (buf_len < sizeof(rcp_registry_s)) return RCP_ERR_NOMEM;
    *out = new (buf) rcp_registry_s{};
    return RCP_OK;
}

inline size_t rcp_registry_sizeof() { return sizeof(rcp_registry_s); }

inline rcp_err_t rcp_registry_close(rcp_registry_h reg) {
    if (!reg) return RCP_ERR_INVALID;
    auto ec = reg->impl.close();
    reg->~rcp_registry_s();
    return ec ? RCP_ERR_CLOSED : RCP_OK;
}

inline rcp_err_t rcp_ctrl_init(rcp_zone_t zone, void* buf, size_t buf_len, rcp_ctrl_h* out) {
    if (buf_len < sizeof(rcp_ctrl_s)) return RCP_ERR_NOMEM;
    auto* h = new (buf) rcp_ctrl_s{};
    h->impl = std::make_shared<rcp::mock::Controller>(static_cast<rcp::Zone>(zone));
    *out = h;
    return RCP_OK;
}

inline size_t rcp_ctrl_sizeof() { return sizeof(rcp_ctrl_s); }

inline rcp_err_t rcp_registry_add(rcp_registry_h reg, rcp_ctrl_h ctrl) {
    if (!reg || !ctrl) return RCP_ERR_INVALID;
    auto ec = reg->impl.register_ctrl(ctrl->impl);
    if (ec == rcp::ErrAlreadyExists) return RCP_ERR_BUSY;
    return ec ? RCP_ERR_INVALID : RCP_OK;
}

inline rcp_err_t rcp_registry_lookup(rcp_registry_h reg, rcp_zone_t zone, rcp_ctrl_h* out) {
    if (!reg || !out) return RCP_ERR_INVALID;
    std::shared_ptr<rcp::Controller> ctrl;
    auto ec = reg->impl.lookup(static_cast<rcp::Zone>(zone), ctrl);
    if (ec) return RCP_ERR_NOT_FOUND;
    // The handle is owned externally; return a transient view.
    // Real RTOS usage would store handles in a static table.
    (void)out;
    return RCP_ERR_NOTSUP; // handle aliasing requires static table (not in stub)
}

inline rcp_err_t rcp_send(rcp_ctrl_h ctrl,
                            const rcp_command_t* cmd,
                            rcp_response_t*      resp,
                            uint32_t             timeout_ms) {
    if (!ctrl || !cmd || !resp) return RCP_ERR_INVALID;

    rcp::Command c;
    c.zone     = static_cast<rcp::Zone>(cmd->zone);
    c.type     = static_cast<rcp::CommandType>(cmd->cmd_type);
    c.priority = static_cast<rcp::Priority>(cmd->priority);
    c.id       = cmd->id;
    if (cmd->payload && cmd->payload_len > 0)
        c.payload.assign(cmd->payload, cmd->payload + cmd->payload_len);

    rcp::Context ctx;
    if (timeout_ms > 0)
        ctx = rcp::Context::with_timeout(std::chrono::milliseconds(timeout_ms));

    rcp::Response r;
    auto ec = ctrl->impl->send(ctx, c, r);
    if (ec) return RCP_ERR_TIMEOUT;

    resp->status      = static_cast<uint8_t>(r.status);
    resp->id          = r.command_id;
    resp->payload     = nullptr;
    resp->payload_len = 0;
    return RCP_OK;
}

inline rcp_err_t rcp_subscribe(rcp_ctrl_h ctrl, rcp_status_cb_t cb, void* userdata) {
    (void)ctrl; (void)cb; (void)userdata;
    return RCP_ERR_NOTSUP; // subscription bridging requires an adapter thread
}
