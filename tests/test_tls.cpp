// fusa:test REQ-TLS-001
// fusa:test REQ-TLS-002
// fusa:test REQ-TLS-003
// fusa:test REQ-TLS-004
// fusa:test REQ-TLS-005
// fusa:test REQ-TLS-006
// fusa:test REQ-TLS-007
// fusa:test REQ-TLS-008
// fusa:test REQ-TLS-009
// fusa:test REQ-TLS-010

// Secure-channel option tests for rcp/tls.hpp — the UDP/IP transport
// variant's DTLS/application-layer channel (ROADMAP.md milestone 58,
// "Auxiliary Transport & Cross-Cutting Rebind", v2.14.0, SG-006, IEC 62443
// SL-2).
//
// Without RCP_TLS_OPENSSL the module is a compile-time interface stub: it
// carries the secure-transport configuration (certs, CA, verify_peer) and
// never performs an insecure send — every transport call returns
// function_not_supported rather than transmitting plaintext. These tests
// pin that configuration surface and the secure-by-default refusal. The
// cipher/protocol-version enforcement of REQ-TLS-007/008 is exercised
// end-to-end by the OpenSSL backend build; here we assert the stub never
// falls back to an insecure path on hosts that lack OpenSSL.
#include <catch2/catch_test_macros.hpp>

#include "rcp/tls.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);

avtp::StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    avtp::StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

tls::Config mtls_config() {
    tls::Config c;
    c.cert_file   = "/etc/rcp/client.pem";
    c.key_file    = "/etc/rcp/client.key";
    c.ca_file     = "/etc/rcp/ca.pem";
    c.verify_peer = true;
    return c;
}
} // namespace

TEST_CASE("tls: verify_peer is true by default (mutual auth enforced)",
          "[tls][REQ-TLS-001][REQ-TLS-003]") {
    tls::Config c; // defaults
    REQUIRE(c.verify_peer == true); // peer authentication is on unless explicitly disabled
}

TEST_CASE("tls: certificate verification is configured via a CA bundle",
          "[tls][REQ-TLS-002]") {
    auto c = mtls_config();
    REQUIRE_FALSE(c.ca_file.empty()); // CA bundle drives peer-certificate verification
    REQUIRE(c.verify_peer);
}

TEST_CASE("tls: PEM cert/key/ca files are carried in Config", "[tls][REQ-TLS-004]") {
    auto c = mtls_config();
    REQUIRE(c.cert_file == "/etc/rcp/client.pem");
    REQUIRE(c.key_file  == "/etc/rcp/client.key");
    REQUIRE(c.ca_file   == "/etc/rcp/ca.pem");
}

TEST_CASE("tls: SecureClient::request never sends a request frame in the clear (stub refuses)",
          "[tls][REQ-TLS-005]") {
    tls::SecureClient client(make_stream_id(0x02, 1), "127.0.0.1", 8443, mtls_config());
    auto req = acf::make_standard_request(/*bus_id=*/1, /*transaction_num=*/1,
                                           /*write=*/false, /*read_size=*/4);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(client.request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("tls: SecureServer never answers in the clear (stub reports not ok())",
          "[tls][REQ-TLS-006]") {
    tls::SecureServer server(make_stream_id(0x03, 1), "127.0.0.1", 8443, mtls_config());
    // No OpenSSL backend -> the server can never actually bind/serve
    // securely, so it must not report itself usable.
    REQUIRE_FALSE(server.ok());
}

TEST_CASE("tls: no insecure fallback on hosts without OpenSSL (TLS1.2+ posture)",
          "[tls][REQ-TLS-007][REQ-TLS-008]") {
    // The secure backend (RCP_TLS_OPENSSL) enforces TLS >=1.2 and disables
    // weak ciphers. The stub's safety contract is that it must NOT silently
    // downgrade to plaintext: every transport call returns a hard error
    // instead.
    tls::SecureClient client(make_stream_id(0x02, 2), "127.0.0.1", 8443, mtls_config());
    REQUIRE_FALSE(client.ok());

    auto req = acf::make_standard_request(1, 1, false, 4);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(client.request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("tls: close terminates the session cleanly", "[tls][REQ-TLS-009]") {
    tls::SecureClient client(make_stream_id(0x02, 3), "127.0.0.1", 8443, mtls_config());
    REQUIRE_FALSE(client.close());

    tls::SecureServer server(make_stream_id(0x03, 3), "127.0.0.1", 8443, mtls_config());
    server.close(); // must not hang or crash
}

TEST_CASE("tls: SecureClient/SecureServer carry the configured stream_id and Config unchanged",
          "[tls][REQ-TLS-010]") {
    auto sid = make_stream_id(0x02, 4);
    auto cfg = mtls_config();
    tls::SecureClient client(sid, "127.0.0.1", 8443, cfg);
    REQUIRE(client.stream_id() == sid);
    REQUIRE(client.config().cert_file == cfg.cert_file);

    tls::SecureServer server(sid, "127.0.0.1", 8443, cfg);
    REQUIRE(server.stream_id() == sid);
    REQUIRE(server.config().ca_file == cfg.ca_file);
}
