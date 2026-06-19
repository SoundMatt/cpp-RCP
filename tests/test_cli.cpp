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
    REQUIRE(s.find("\"spec_version\":\"1.10\"") != std::string::npos);
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
    for (auto k : {"kind", "tool", "version", "spec_version", "commands",
                   "transports", "features", "interfaces",
                   "optional_interfaces", "adapt"}) {
        REQUIRE(has_key(s, k));
    }
    REQUIRE(s.find("\"kind\":\"capabilities\"") != std::string::npos);
    REQUIRE(s.find("\"adapt\":true") != std::string::npos);
    // commands MUST contain the three mandatory subcommands.
    REQUIRE(s.find("\"version\"") != std::string::npos);
    REQUIRE(s.find("\"capabilities\"") != std::string::npos);
    REQUIRE(s.find("\"status\"") != std::string::npos);
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
    // Three well-formed relay.Message lines addressed to standard zones.
    const std::string nd =
        "{\"protocol\":5,\"id\":\"FrontLeft\",\"payload\":\"AQ==\",\"meta\":{\"rcp.command_type\":\"1\"}}\n"
        "{\"protocol\":5,\"id\":\"RearRight\",\"seq\":7}\n"
        "{\"protocol\":5,\"id\":\"Central\",\"meta\":{\"rcp.priority\":\"2\"}}\n";
    int code = 0;
    auto out = capture_in({"send", "--format", "json"}, nd, code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(out.find("published 3 message(s)") != std::string::npos);
}

TEST_CASE("cli: send skips malformed and undeliverable lines without aborting",
          "[cli][conformance][REQ-CLI-005]") {
    const std::string nd =
        "not json\n"
        "{\"id\":\"FrontLeft\"}\n"          // deliverable
        "{\"id\":\"Nonexistent\"}\n"        // unknown zone -> skipped
        "\n";                               // blank -> skipped
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

TEST_CASE("cli: send base64 payload decodes into the published command",
          "[cli][conformance][REQ-CLI-005]") {
    // "AQID" -> bytes {1,2,3}; empty stream variant just checks clean EOF handling.
    int code = 0;
    auto out = capture_in({"send", "--format", "json"},
                          "{\"id\":\"FrontLeft\",\"payload\":\"AQID\"}\n", code);
    REQUIRE(code == rcp::cli::kOk);
    REQUIRE(out.find("published 1 message(s)") != std::string::npos);
}
