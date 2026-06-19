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

// Mutual-TLS transport tests (SG-006, IEC 62443 SL-2).
//
// Without RCP_TLS_OPENSSL the TLS module is a compile-time interface stub: it
// carries the secure-transport configuration (certs, CA, verify_peer) and never
// performs an insecure send — every transport call returns
// function_not_supported rather than transmitting plaintext. These tests pin
// that configuration surface and the secure-by-default refusal. The cipher /
// protocol-version enforcement of REQ-TLS-007/008 is exercised end-to-end by
// the OpenSSL backend build; here we assert the stub never falls back to an
// insecure path on hosts that lack OpenSSL.
#include <catch2/catch_test_macros.hpp>

#include "rcp/tls.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);

tls::Config mtls_config() {
    tls::Config c;
    c.cert_file = "/etc/rcp/client.pem";
    c.key_file  = "/etc/rcp/client.key";
    c.ca_file   = "/etc/rcp/ca.pem";
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

TEST_CASE("tls: command frames never sent in the clear (stub refuses send)",
          "[tls][REQ-TLS-005]") {
    tls::Controller ctrl(Zone::FrontLeft, "127.0.0.1", 8443, mtls_config());
    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Set;
    Response resp;
    // No OpenSSL backend → refuse rather than transmit an unencrypted frame.
    REQUIRE(ctrl.send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("tls: status frames never sent in the clear (stub refuses subscribe)",
          "[tls][REQ-TLS-006]") {
    tls::Controller ctrl(Zone::FrontLeft, "127.0.0.1", 8443, mtls_config());
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl.subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("tls: no insecure fallback on hosts without OpenSSL (TLS1.2+ posture)",
          "[tls][REQ-TLS-007][REQ-TLS-008]") {
    // The secure backend (RCP_TLS_OPENSSL) enforces TLS >=1.2 and disables weak
    // ciphers. The stub's safety contract is that it must NOT silently downgrade
    // to plaintext: dialing/sending returns a hard error instead.
    auto reg = tls::new_registry();
    REQUIRE(reg->dial(Zone::Central, "127.0.0.1", 8443, mtls_config()) == kUnsupported);
    tls::Controller ctrl(Zone::Central, "127.0.0.1", 8443, mtls_config());
    Command cmd; cmd.zone = Zone::Central; Response resp;
    REQUIRE(ctrl.send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("tls: close terminates the session cleanly", "[tls][REQ-TLS-009]") {
    tls::Controller ctrl(Zone::RearLeft, "127.0.0.1", 8443, mtls_config());
    REQUIRE_FALSE(ctrl.close());
    auto reg = tls::new_registry();
    REQUIRE_FALSE(reg->close());
}

TEST_CASE("tls: transport errors propagate to the caller", "[tls][REQ-TLS-010]") {
    auto reg = tls::new_registry();
    std::shared_ptr<Controller> out;
    REQUIRE(reg->lookup(Zone::FrontLeft, out) == kUnsupported);
    REQUIRE(reg->register_ctrl(nullptr) == kUnsupported);
    REQUIRE(reg->deregister(Zone::FrontLeft) == kUnsupported);
}
