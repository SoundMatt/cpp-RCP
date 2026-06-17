// fusa:req REQ-CLI-001
// fusa:req REQ-CLI-002
// fusa:req REQ-CLI-003
// fusa:req REQ-CLI-004

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
    REQUIRE(s.find("\"spec_version\":\"0.3\"") != std::string::npos);
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
