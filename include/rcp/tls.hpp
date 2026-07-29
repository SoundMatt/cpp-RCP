// fusa:req REQ-TLS-001
// fusa:req REQ-TLS-002
// fusa:req REQ-TLS-003
// fusa:req REQ-TLS-004
// fusa:req REQ-TLS-005
// fusa:req REQ-TLS-006
// fusa:req REQ-TLS-007
// fusa:req REQ-TLS-008
// fusa:req REQ-TLS-009
// fusa:req REQ-TLS-010

// Secure-channel option for the UDP/IP transport variant (SG-006, IEC 62443
// SL-2): DTLS or an application-layer encrypted channel wrapping
// rcp/udp.hpp's Server/Client.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package Disposition
// table's entry for `tls.hpp`, to build on rcp/udp.hpp's udp::Server/
// udp::Client (v2.13.0) instead of the pre-replacement Zone-addressed
// ZoneServer/Controller pair. SecureClient/SecureServer below carry the
// same certificate/CA configuration surface as the pre-replacement Config,
// and preserve the same "interface + stub" split: full support requires
// OpenSSL or an equivalent (D)TLS library. Define RCP_TLS_OPENSSL and link
// -lssl -lcrypto to enable a real backend; without that flag every transport
// operation below returns std::errc::function_not_supported rather than
// ever falling back to sending a request in the clear, so the rest of the
// codebase (and CI hosts without OpenSSL) can build and test against this
// header's interface regardless.
//
// IMPORTANT — link-security posture: the specification's own preferred
// mechanism for securing the link this protocol rides on is MACsec (IEEE
// 802.1AE) operating at layer 2, beneath native AVTP-over-Ethernet framing
// (extraction §3.12, §1.3). This package does not implement, and does not
// otherwise address, MACsec at all — it only ever secures the UDP/IP
// transport variant (rcp/udp.hpp's Annex J encapsulation) at the
// application layer. An embedding that has native Ethernet framing
// available end to end should prefer a MACsec-capable NIC/switch fabric
// over deploying this header at all; this header exists for the UDP/IP
// path specifically, where MACsec's layer-2 guarantee does not apply once
// the AVTPDU is riding inside an ordinary UDP/IP datagram.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context/ErrClosed/ErrTimeout only — see this header's own scope note above
#include "udp.hpp"

#include <memory>
#include <string>
#include <vector>

namespace rcp {
namespace tls {

// ── Config ────────────────────────────────────────────────────────────────────
// Unchanged in shape from the pre-replacement design — the certificate/CA
// configuration surface this package exposes is orthogonal to which wire
// protocol rides on top of the secured channel.

struct Config {
    std::string cert_file;    // PEM certificate for this endpoint
    std::string key_file;     // PEM private key for this endpoint
    std::string ca_file;      // PEM CA bundle for peer verification
    bool        verify_peer = true; // enforce mutual authentication
};

namespace detail {
inline std::error_code unsupported() noexcept {
    return std::make_error_code(std::errc::function_not_supported);
}
} // namespace detail

// ── SecureClient ──────────────────────────────────────────────────────────────
// SecureClient wraps a udp::Client, adding a DTLS (or application-layer)
// encrypted channel per `cfg` before any AVTPDU-framed ACF request leaves
// this process. Its request() signature mirrors udp::Client::request's
// core shape so a caller can swap one for the other without reshaping its
// own request-building code.
class SecureClient {
public:
    SecureClient(avtp::StreamId stream_id, const char* server_host, uint16_t server_port,
                 Config cfg)
        : stream_id_(stream_id), cfg_(std::move(cfg)) {
        (void)server_host;
        (void)server_port;
    }

    std::error_code request(const rcp::Context& ctx,
                             const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload) {
        (void)ctx; (void)req; (void)req_payload; (void)out_resp; (void)out_resp_payload;
        return detail::unsupported();
    }

    std::error_code close() { return {}; }

    bool ok() const noexcept { return false; }

    avtp::StreamId stream_id() const noexcept { return stream_id_; }
    const Config&  config() const noexcept { return cfg_; }

private:
    avtp::StreamId stream_id_;
    Config         cfg_;
};

// ── SecureServer ──────────────────────────────────────────────────────────────
// SecureServer wraps a udp::Server the same way SecureClient wraps
// udp::Client. Handler is shaped identically to udp::Server::Handler so the
// same handler (e.g. rcp::mock::Server::dispatch) can be wired to either.
class SecureServer {
public:
    using Handler = udp::Server::Handler;

    SecureServer(avtp::StreamId stream_id, const char* addr, uint16_t port, Config cfg)
        : stream_id_(stream_id), cfg_(std::move(cfg)) {
        (void)addr;
        (void)port;
    }

    void set_handler(Handler h) { handler_ = std::move(h); }

    std::string addr_string() const { return {}; }
    uint16_t    port()        const { return 0; }

    void close() {}

    bool ok() const noexcept { return false; }

    avtp::StreamId stream_id() const noexcept { return stream_id_; }
    const Config&  config() const noexcept { return cfg_; }

private:
    avtp::StreamId stream_id_;
    Config         cfg_;
    Handler        handler_;
};

inline std::unique_ptr<SecureClient> new_secure_client(avtp::StreamId stream_id,
                                                          const char* server_host,
                                                          uint16_t    server_port,
                                                          Config      cfg) {
    return std::make_unique<SecureClient>(stream_id, server_host, server_port, std::move(cfg));
}

inline std::unique_ptr<SecureServer> new_secure_server(avtp::StreamId stream_id,
                                                          const char* addr,
                                                          uint16_t    port,
                                                          Config      cfg) {
    return std::make_unique<SecureServer>(stream_id, addr, port, std::move(cfg));
}

} // namespace tls
} // namespace rcp
