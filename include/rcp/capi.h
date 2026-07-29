/*
 * fusa:req REQ-CAPI-001
 * fusa:req REQ-CAPI-002
 * fusa:req REQ-CAPI-003
 * fusa:req REQ-CAPI-004
 * fusa:req REQ-CAPI-005
 * fusa:req REQ-CAPI-006
 * fusa:req REQ-CAPI-007
 * fusa:req REQ-CAPI-008
 * fusa:req REQ-CAPI-009
 *
 * cpp-RCP C API for RTOS / bare-metal integration.
 *
 * ROADMAP.md milestone 60, "C ABI & CLI Rebuild (v2.16.0)": this header
 * REPLACES its pre-replacement content in full, per the Satellite Package
 * Disposition table's entry for `capi.h`/`capi_impl.hpp` — the old
 * rcp_zone_t/rcp_command_t/rcp_response_t triple was Zone/CommandType-
 * specific and had no way to generalize by adaptation once Zone/Command are
 * gone, so this is a from-scratch redesign against the server+endpoint
 * addressing and ACF request/response shape established through v2.15.0
 * (rcp/acf.hpp, v2.0.0), not an incremental patch of the old structs.
 *
 * Addressing (extraction §2.1, via rcp/acf.hpp/rcp/avtp.hpp): a request
 * targets one endpoint via a caller-chosen rcp_stream_key_t (typically an
 * rcp::avtp::StreamId's to_u64() form) plus an rcp_byte_bus_id_t, unique
 * only within that stream_key. rcp_acf_info_t mirrors rcp::acf::
 * AcfMessageInfo's shared-header fields one-for-one (see rcp/acf.hpp) so a
 * caller building a request here needs no second field-by-field mapping
 * doc; unlike the removed rcp_priority_t, there is no separate priority
 * field — the target specification's own execution-priority ordering is a
 * server-side property of the request *kind* (rcp/request.hpp, v2.5.0),
 * not a client-supplied hint.
 *
 * This C ABI does not itself implement a transport, a server, or a
 * request-dispatch backend: an rcp_ctrl_h binds one caller-supplied
 * rcp_request_fn_t callback (plus opaque userdata) to one stream_key/
 * byte_bus_id pair, and rcp_send() is a direct, allocation-free call
 * through to that callback. This mirrors the "client-side send-equivalent
 * call" shape rcp/record.hpp's and rcp/observe.hpp's C++-level `RequestFn`
 * already standardize on for the same purpose (v2.14.0) — expressed here as
 * a plain C function pointer, since C has no std::function, so the same
 * callback can be backed by rcp::mock::Server::dispatch (v2.12.0), by
 * rcp::udp::Client::request (v2.13.0), or by real hardware, without this
 * header depending on any of them.
 *
 * All buffers are caller-supplied, including the response payload output
 * buffer (rcp_response_t::payload / payload_cap below) — no heap
 * allocation occurs inside this header or its implementation
 * (rcp/capi_impl.hpp) at any point.
 *
 * Suitable for:
 *   - AUTOSAR Classic / OSEK RTOS (no C++ runtime)
 *   - FreeRTOS / Zephyr (minimal C++ support)
 *   - Safety MCUs with C-only toolchains
 *
 * Thread safety: the opaque handle types are not thread-safe. Callers must
 * provide external locking if multiple tasks share a handle.
 *
 * Field names and behavior below implement the OPEN Alliance TC18 Remote
 * Control Protocol Specification v0.5.1_RC's *behavior* as described in an
 * internal structured extraction of that specification; no text from that
 * document is reproduced here.
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

/* ── Addressing (v2.16.0): server + endpoint, not Zone ───────────────────── */

/* rcp_stream_key_t is the opaque per-connection identifier a request
 * targets — typically rcp::avtp::StreamId::to_u64() on the C++ side, but
 * this header does not require that origin. */
typedef uint64_t rcp_stream_key_t;

/* rcp_byte_bus_id_t addresses one endpoint within a stream_key (extraction
 * §2.1) — unique only within the owning stream_key, never globally. */
typedef uint8_t rcp_byte_bus_id_t;

/* ── ACF shared header, flattened for C linkage (rcp/acf.hpp AcfMessageInfo) */

typedef struct {
    uint8_t           acf_msg_type;             /* ACF_ABB (0x0E) or ACF_GBB (0x0D) */
    uint16_t          acf_msg_length;            /* quadlets, including this header */
    uint8_t           pad;                       /* 0-3 trailing pad bytes */
    uint8_t           mtv;                       /* bool: message_timestamp valid (ACF_GBB only) */
    rcp_byte_bus_id_t byte_bus_id;                /* target endpoint within stream_key */
    uint8_t           evt_ack;                   /* bool: evt[3] acknowledge flag */
    uint8_t           evt_op;                     /* evt[2:0]: endpoint-defined sub-opcode */
    uint8_t           hs;                         /* bool: endpoint-specific reserved bit */
    uint8_t           cs;                         /* bool: conditional-start (request-kind-specific) */
    uint8_t           transaction_num;            /* correlates a request with its response/ack */
    uint8_t           op;                         /* bool: false = read, true = write */
    uint8_t           rsp;                        /* bool: set on every response/ack */
    uint8_t           err;                        /* bool: set alongside rsp for an error response */
    uint8_t           ms;                         /* bool: "more segments" (fragmentation; unused) */
    uint16_t          read_size_or_segment_num;   /* read_size when !ms, segment_num when ms */
} rcp_acf_info_t;

/* ── Request / Response ───────────────────────────────────────────────────── */

typedef struct {
    rcp_stream_key_t stream_key;
    rcp_acf_info_t    info;
    const uint8_t*    payload;      /* caller-owned input bytes; may be NULL iff payload_len == 0 */
    uint32_t          payload_len;
} rcp_request_t;

typedef struct {
    rcp_acf_info_t info;
    uint8_t*       payload;         /* caller-supplied output buffer of payload_cap bytes */
    uint32_t       payload_cap;     /* capacity of `payload`, set by the caller before rcp_send() */
    uint32_t       payload_len;     /* bytes actually written; always <= payload_cap on RCP_OK */
} rcp_response_t;

/* ── Opaque handles ───────────────────────────────────────────────────────── */

typedef struct rcp_registry_s* rcp_registry_h;
typedef struct rcp_ctrl_s*     rcp_ctrl_h;

/*
 * rcp_request_fn_t is the caller-supplied client-side send-equivalent call
 * an rcp_ctrl_h binds to one stream_key/byte_bus_id — see this header's own
 * comment above for why its shape mirrors rcp/record.hpp's and
 * rcp/observe.hpp's C++-level RequestFn. `resp->payload`/`payload_cap` are
 * already populated by the caller of rcp_send() when this callback runs;
 * the callback MUST NOT write more than `resp->payload_cap` bytes and MUST
 * set `resp->payload_len` to the number of bytes actually written.
 */
typedef rcp_err_t (*rcp_request_fn_t)(void*                 userdata,
                                       const rcp_request_t*  req,
                                       rcp_response_t*       resp,
                                       uint32_t              timeout_ms);

/* ── Registry ─────────────────────────────────────────────────────────────── */
/* A registry is a small fixed-capacity table of rcp_ctrl_h entries keyed by
 * (stream_key, byte_bus_id) — the same role the pre-replacement registry
 * played keyed by rcp_zone_t, now generalized to the new addressing model.
 * Capacity is fixed at rcp_registry_sizeof() bytes; there is no dynamic
 * growth, so registry storage is exactly as heap-free as an individual
 * rcp_ctrl_h. */

/*
 * rcp_registry_init initialises a registry in caller-supplied memory.
 *   buf      – static buffer of at least rcp_registry_sizeof() bytes
 *   buf_len  – buffer length in bytes
 * Returns RCP_OK on success, RCP_ERR_NOMEM if buf_len is too small.
 */
rcp_err_t rcp_registry_init(void* buf, size_t buf_len, rcp_registry_h* out);

/* rcp_registry_sizeof returns the required buffer size for a registry. */
size_t rcp_registry_sizeof(void);

/* rcp_registry_close closes the registry. Registered rcp_ctrl_h entries are
 * caller-owned (placement-new'd into caller buffers) and are not touched. */
rcp_err_t rcp_registry_close(rcp_registry_h reg);

/* ── Controller ───────────────────────────────────────────────────────────── */

/*
 * rcp_ctrl_init binds one (stream_key, byte_bus_id) target to one
 * rcp_request_fn_t callback, in caller-supplied memory.
 *   stream_key / byte_bus_id – the endpoint this handle addresses
 *   fn                       – the send-equivalent callback; must not be NULL
 *   userdata                 – opaque pointer forwarded to every fn() call
 *   buf                      – static buffer of at least rcp_ctrl_sizeof() bytes
 * Returns RCP_OK on success, RCP_ERR_NOMEM if buf_len is too small,
 * RCP_ERR_INVALID if fn is NULL.
 */
rcp_err_t rcp_ctrl_init(rcp_stream_key_t  stream_key,
                         rcp_byte_bus_id_t byte_bus_id,
                         rcp_request_fn_t  fn,
                         void*             userdata,
                         void*             buf,
                         size_t            buf_len,
                         rcp_ctrl_h*       out);
size_t    rcp_ctrl_sizeof(void);

/* rcp_registry_add registers ctrl under its own (stream_key, byte_bus_id).
 * Returns RCP_ERR_BUSY if that pair is already registered, RCP_ERR_NOMEM if
 * the registry's fixed capacity is exhausted. */
rcp_err_t rcp_registry_add(rcp_registry_h reg, rcp_ctrl_h ctrl);

/* rcp_registry_lookup looks up the ctrl registered for (stream_key, byte_bus_id). */
rcp_err_t rcp_registry_lookup(rcp_registry_h     reg,
                               rcp_stream_key_t   stream_key,
                               rcp_byte_bus_id_t  byte_bus_id,
                               rcp_ctrl_h*        out);

/* ── Send ─────────────────────────────────────────────────────────────────── */

/*
 * rcp_send issues one request through ctrl's bound rcp_request_fn_t and
 * blocks until it returns.
 *   ctrl        – handle from rcp_ctrl_init (or rcp_registry_lookup)
 *   req         – request to send; req->info.byte_bus_id conventionally
 *                 matches ctrl's own byte_bus_id, but this call does not
 *                 enforce that — ctrl only supplies the transport callback
 *   resp        – output: resp->payload/payload_cap MUST already point at a
 *                 caller-supplied buffer on entry; populated on success
 *   timeout_ms  – forwarded to fn unchanged; 0 = no timeout
 * Returns RCP_ERR_INVALID if any pointer argument is NULL, or if the
 * callback reports more response bytes than resp->payload_cap allowed.
 */
rcp_err_t rcp_send(rcp_ctrl_h            ctrl,
                    const rcp_request_t* req,
                    rcp_response_t*      resp,
                    uint32_t             timeout_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RCP_CAPI_H */
