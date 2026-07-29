// fusa:test REQ-CLI-001
// fusa:test REQ-CLI-002
// fusa:test REQ-CLI-003
// fusa:test REQ-CLI-004
// fusa:test REQ-CLI-005

// RELAY CLI conformance tests (spec §11 command surface, §12 JSON documents).
//
// Drives rcp::cli::run() directly (no subprocess) and checks the emitted JSON
// against the required fields of RELAY spec/schemas/cli-*.json, plus the §11.3
// exit codes.
//
// ROADMAP.md milestone 60 (v2.16.0): the `send` coverage below is rewritten
// against the new `--server <stream_key> --endpoint <byte_bus_id> --op
// read|write` protocol-flags form and the mock::Server-backed streaming
// sink — see rcp/cli.hpp's own header comment. `version`/`capabilities`/
// `status` coverage is unchanged in shape, per this milestone's own scope
// note, with `capabilities`'s transports/features assertions refreshed to
// the v2.16.0 field values.
#include <catch2/catch_test_macros.hpp>

#include <rcp/cli.hpp>

#include <sstream>
#include <string>
#include <vector>

using rcp::cli::run;

namespace {

// has_key returns true if `"key"` appears as a JSON object key in s.
bool has_key(const std::string& s, const std::string& key) {
    return s.find("\"" + key + "\"") != std::string::npos;
}

// braces_balanced is a minimal structural sanity check on emitted JSON.
bool braces_balanced(const std::string& s) {
    int depth = 0;
    for (char c : s) {
        if (c == '{') ++depth;
        else if (c == '}') --depth;
        if (depth < 0) return false;
    }
    return depth == 0;
}

std::string capture(const std::vector<std::string>& args, int& code) {
    std::ostringstream out, err;
    code = run(args, out, err);
    return out.str();
}

// capture_in drives the streaming overload with NDJSON on stdin.
std::string capture_in(const std::vector<std::string>& args, const std::string& stdin_data,
                       int& code, std::string* errout = nullptr) {
    std::istringstream in(stdin_data);
    std::ostringstream out, err;
    code = run(args, in, out, err);
    if (errout) *errout = err.str();
    return out.str();
}

} // namespace

// ── §11.3 exit codes ──────────────────────────────────────────────────────────

TEST_CASE("cli: no command returns invalid-args (2)", "[cli][conformance]") {
    int code = 0;
    capture({}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: unknown command returns invalid-args (2)", "[cli][conformance]") {
    int code = 0;
    capture({"frobnicate"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: invalid --format returns invalid-args (2)", "[cli][conformance]") {
    int code = 0;
    capture({"version", "--format", "yaml"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

// ── §12.1 version ─────────────────────────────────────────────────────────────

TEST_CASE("cli: version --format json has all required fields", "[cli][conformance]") {
    int code = 0;
    auto s = capture({"version", "--format", "json"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(braces_balanced(s));
    for (auto k : {"tool", "version", "spec_version", "language", "runtime"}) {
        REQUIRE(has_key(s, k));
    }
    REQUIRE(s.find("\"language\":\"cpp\"") != std::string::npos);
    REQUIRE(s.find("\"spec_version\":\"1.11\"") != std::string::npos);
    REQUIRE(s.find("\"protocol_int\":5") != std::string::npos);
}

TEST_CASE("cli: version default format is text", "[cli][conformance]") {
    int code = 0;
    auto s = capture({"version"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(s.find("cpp-rcp") != std::string::npos);
    REQUIRE(s.find("{") == std::string::npos); // not JSON
}

// ── §12.2 capabilities ────────────────────────────────────────────────────────

TEST_CASE("cli: capabilities has all required fields and kind", "[cli][conformance]") {
    int code = 0;
    auto s = capture({"capabilities"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(braces_balanced(s));
    for (auto k : {"kind", "tool", "protocol", "protocol_int", "version",
                   "spec_version", "commands", "transports", "features",
                   "interfaces", "optional_interfaces", "adapt"}) {
        REQUIRE(has_key(s, k));
    }
    REQUIRE(s.find("\"kind\":\"capabilities\"") != std::string::npos);
    REQUIRE(s.find("\"protocol\":\"RCP\"") != std::string::npos);
    REQUIRE(s.find("\"protocol_int\":5") != std::string::npos);
    REQUIRE(s.find("\"adapt\":true") != std::string::npos);
    // commands MUST contain the three mandatory subcommands.
    REQUIRE(s.find("\"version\"") != std::string::npos);
    REQUIRE(s.find("\"capabilities\"") != std::string::npos);
    REQUIRE(s.find("\"status\"") != std::string::npos);
}

TEST_CASE("cli: capabilities transports/features reflect the v2.16.0 rebuild",
          "[cli][conformance]") {
    int code = 0;
    auto s = capture({"capabilities"}, code);
    REQUIRE(code == rcp::cli::kOk);
    // Transports: the native UDP/IP (v2.13.0), shmem (v2.14.0), and the
    // in-process mock::Server (v2.12.0) demo backend `send` dispatches to.
    REQUIRE(s.find("\"udp\"") != std::string::npos);
    REQUIRE(s.find("\"shmem\"") != std::string::npos);
    REQUIRE(s.find("\"mock\"") != std::string::npos);
    // Features: a sample of the endpoint set from v2.3.0/v2.4.0/v2.7.0.
    REQUIRE(s.find("\"gpio\"") != std::string::npos);
    REQUIRE(s.find("\"spi\"") != std::string::npos);
    REQUIRE(s.find("\"can\"") != std::string::npos);
    REQUIRE(s.find("\"loaning\"") != std::string::npos);
}

// ── §12.3 status ──────────────────────────────────────────────────────────────

TEST_CASE("cli: status --format json has all required fields", "[cli][conformance]") {
    int code = 0;
    auto s = capture({"status", "--format", "json"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(braces_balanced(s));
    for (auto k : {"tool", "version", "healthy", "connected", "endpoint", "details"}) {
        REQUIRE(has_key(s, k));
    }
    REQUIRE(s.find("\"healthy\":true") != std::string::npos);
    REQUIRE(s.find("\"connected\":false") != std::string::npos);
    REQUIRE(s.find("\"details\":{}") != std::string::npos);
}

// ── §11.2 streaming send --format json sink (crossbar spoke) ──────────────────

TEST_CASE("cli: send is declared in capabilities", "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    auto s = capture({"capabilities"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(s.find("\"send\"") != std::string::npos);
}

TEST_CASE("cli: send --format json publishes each NDJSON message", "[cli][conformance][REQ-CLI-005]") {
    // Three well-formed relay.Message lines addressed to the mock GPIO/SPI
    // endpoints (byte_bus_id 1 and 2) via the v2.16.0 "<16 hex>:<decimal>" id.
    // SPI (endpoint 2) accepts any payload length, unlike GPIO's fixed
    // 4-byte bitmask, so short payloads are addressed there.
    const std::string nd =
        "{\"protocol\":5,\"id\":\"0000000000000000:2\",\"payload\":\"AQ==\",\"meta\":{\"rcp.op\":\"write\"}}\n"
        "{\"protocol\":5,\"id\":\"0000000000000000:2\",\"seq\":7}\n"
        "{\"protocol\":5,\"id\":\"0000000000000000:1\",\"meta\":{\"rcp.op\":\"read\"}}\n";
    int code = 0;
    auto out = capture_in({"send", "--format", "json"}, nd, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(out.find("published 3 message(s)") != std::string::npos);
}

TEST_CASE("cli: send skips malformed and undeliverable lines without aborting",
          "[cli][conformance][REQ-CLI-005]") {
    const std::string nd =
        "not json\n"
        "{\"id\":\"0000000000000000:1\"}\n"     // deliverable (GPIO)
        "{\"id\":\"0000000000000000:99\"}\n"    // unregistered endpoint -> skipped
        "{\"id\":\"not-a-valid-id\"}\n"          // unparseable id -> skipped
        "\n";                                     // blank -> skipped
    int code = 0;
    std::string errout;
    auto out = capture_in({"send", "--format", "json"}, nd, code, &errout);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(out.find("published 1 message(s)") != std::string::npos);
    REQUIRE(errout.find("skipping malformed") != std::string::npos);
}

TEST_CASE("cli: send without --format json returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture_in({"send"}, "", code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send base64 payload decodes into the published request",
          "[cli][conformance][REQ-CLI-005]") {
    // "AQID" -> bytes {1,2,3}; SPI (endpoint 2) accepts any payload length.
    int code = 0;
    auto out = capture_in({"send", "--format", "json"},
                          "{\"id\":\"0000000000000000:2\",\"payload\":\"AQID\","
                          "\"meta\":{\"rcp.op\":\"write\"}}\n",
                          code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(out.find("published 1 message(s)") != std::string::npos);
}

// ── §11.2 protocol-flags send: --server/--endpoint/--op (RCP row) ─────────────

TEST_CASE("cli: send --server --endpoint --op read dispatches a single request (text)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    auto s = capture({"send", "--server", "0", "--endpoint", "1", "--op", "read"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(s.find("0000000000000000:1") != std::string::npos);
}

TEST_CASE("cli: send --server --endpoint --op write --payload dispatches (json)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    auto s = capture({"send", "--server", "0x2A", "--endpoint", "2", "--op", "write",
                      "--payload", "0102", "--format", "json"}, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(s.find("\"sent\":true") != std::string::npos);
    REQUIRE(s.find("\"id\":\"000000000000002a:2\"") != std::string::npos);
}

TEST_CASE("cli: send missing --op returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "1"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send with an unrecognised --op returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "1", "--op", "bogus"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send with malformed --payload hex returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "1", "--op", "write",
             "--payload", "zz"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send with an out-of-range --evt-op returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "1", "--op", "read",
             "--evt-op", "8"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send to an unregistered endpoint returns a protocol error (1)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "99", "--op", "read"}, code);
    REQUIRE(code == rcp::cli::kError);
}

TEST_CASE("cli: send with an invalid --server value returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "not-a-number", "--endpoint", "1", "--op", "read"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}

TEST_CASE("cli: send with an invalid --endpoint value returns invalid-args (2)",
          "[cli][conformance][REQ-CLI-005]") {
    int code = 0;
    capture({"send", "--server", "0", "--endpoint", "300", "--op", "read"}, code);
    REQUIRE(code == rcp::cli::kInvalidArgs);
}
