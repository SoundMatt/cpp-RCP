/*
 * fusa:req REQ-CAPI-001
 * fusa:req REQ-CAPI-002
 * fusa:req REQ-CAPI-003
 * fusa:req REQ-CAPI-004
 * fusa:req REQ-CAPI-005
 * fusa:req REQ-CAPI-006
 * fusa:req REQ-CAPI-007
 * fusa:req REQ-CAPI-008
 *
 * cpp-RCP C API for RTOS / bare-metal integration (v0.40.0).
 *
 * This header exposes a C-linkage flat API suitable for:
 *   - AUTOSAR Classic / OSEK RTOS (no C++ runtime)
 *   - FreeRTOS / Zephyr (minimal C++ support)
 *   - Safety MCUs with C-only toolchains
 *
 * All buffers are caller-supplied.  No heap allocation occurs inside the
 * library once rcp_registry_init() has been called.
 *
 * Thread safety: the opaque handle types are not thread-safe.  Callers must
 * provide external locking if multiple tasks share a handle.
 */

#ifndef RCP_CAPI_H
#define RCP_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ── Error codes ──────────────────────────────────────────────────────────── */

typedef int rcp_err_t;

#define RCP_OK              0
#define RCP_ERR_TIMEOUT     1
#define RCP_ERR_NOT_FOUND   2
#define RCP_ERR_CLOSED      3
#define RCP_ERR_BUSY        4
#define RCP_ERR_NOMEM       5
#define RCP_ERR_INVALID     6
#define RCP_ERR_NOTSUP      7

/* ── Zone identifiers ─────────────────────────────────────────────────────── */

typedef uint8_t rcp_zone_t;

#define RCP_ZONE_FRONT_LEFT   0
#define RCP_ZONE_FRONT_RIGHT  1
#define RCP_ZONE_REAR_LEFT    2
#define RCP_ZONE_REAR_RIGHT   3
#define RCP_ZONE_CENTRAL      4

/* ── Priority ─────────────────────────────────────────────────────────────── */

typedef uint8_t rcp_priority_t;

#define RCP_PRIORITY_LOW      0
#define RCP_PRIORITY_NORMAL   1
#define RCP_PRIORITY_HIGH     2
#define RCP_PRIORITY_CRITICAL 3

/* ── Command / Response ───────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t     zone;
    uint16_t       cmd_type;
    rcp_priority_t priority;
    uint8_t        reserved;
    uint32_t       id;
    const uint8_t* payload;
    uint32_t       payload_len;
} rcp_command_t;

typedef struct {
    uint8_t        status;
    uint32_t       id;
    uint8_t*       payload;
    uint32_t       payload_len;
} rcp_response_t;

/* ── Opaque handles ───────────────────────────────────────────────────────── */

typedef struct rcp_registry_s* rcp_registry_h;
typedef struct rcp_ctrl_s*     rcp_ctrl_h;

/* ── Registry ─────────────────────────────────────────────────────────────── */

/*
 * rcp_registry_init initialises a registry in caller-supplied memory.
 *   buf      – static buffer of at least rcp_registry_sizeof() bytes
 *   buf_len  – buffer length in bytes
 * Returns RCP_OK on success, RCP_ERR_NOMEM if buf_len is too small.
 */
rcp_err_t rcp_registry_init(void* buf, size_t buf_len, rcp_registry_h* out);

/* rcp_registry_sizeof returns the required buffer size for a registry. */
size_t rcp_registry_sizeof(void);

/* rcp_registry_close closes the registry and all registered controllers. */
rcp_err_t rcp_registry_close(rcp_registry_h reg);

/* ── Controller ───────────────────────────────────────────────────────────── */

/*
 * rcp_ctrl_init initialises a mock controller in caller-supplied memory.
 *   zone – zone this controller serves
 *   buf  – static buffer of at least rcp_ctrl_sizeof() bytes
 * Returns RCP_OK on success.
 */
rcp_err_t rcp_ctrl_init(rcp_zone_t zone, void* buf, size_t buf_len, rcp_ctrl_h* out);
size_t    rcp_ctrl_sizeof(void);

/* rcp_registry_add registers a controller with the registry. */
rcp_err_t rcp_registry_add(rcp_registry_h reg, rcp_ctrl_h ctrl);

/* rcp_registry_lookup looks up a controller for zone. */
rcp_err_t rcp_registry_lookup(rcp_registry_h reg, rcp_zone_t zone, rcp_ctrl_h* out);

/* ── Send ─────────────────────────────────────────────────────────────────── */

/*
 * rcp_send sends a command and blocks until a response is received or
 * timeout_ms elapses.
 *   ctrl        – controller handle from rcp_registry_lookup
 *   cmd         – command to send
 *   resp        – output: response populated on success
 *   timeout_ms  – deadline in milliseconds; 0 = no timeout
 */
rcp_err_t rcp_send(rcp_ctrl_h     ctrl,
                   const rcp_command_t* cmd,
                   rcp_response_t*      resp,
                   uint32_t             timeout_ms);

/* ── Status subscribe ─────────────────────────────────────────────────────── */

typedef void (*rcp_status_cb_t)(rcp_zone_t zone, uint8_t status, void* userdata);

rcp_err_t rcp_subscribe(rcp_ctrl_h ctrl, rcp_status_cb_t cb, void* userdata);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RCP_CAPI_H */
