// fusa:req REQ-CLI-001
// fusa:req REQ-CLI-002
// fusa:req REQ-CLI-003
// fusa:req REQ-CLI-004

// RELAY-conformant CLI for cpp-RCP (spec §11 CLI contract, §12 capability docs).
//
// Implements the three mandatory commands — `version`, `capabilities`, `status` —
// emitting JSON that conforms to the RELAY spec/schemas/cli-*.json schemas so
// `relay conform` / `relay report` can validate the binary.
//
// The logic lives in this header as rcp::cli::run() so it is unit-testable
// without spawning a subprocess; cli/main.cpp is a thin wrapper around it.
//
// Binary name: `cpp-rcp` (§13.2). Build with -DRELAY_BUILD_CLI=ON.
#pragma once

#include <relay/relay.hpp>
#include <rcp/version.hpp>

#include <ostream>
#include <string>
#include <vector>

namespace rcp {
namespace cli {

// Exit codes per §11.3.
enum : int { kOk = 0, kError = 1, kInvalidArgs = 2 };

// runtime_string returns the compiler id + major version (§12.1 "runtime").
inline std::string runtime_string() {
#if defined(__clang__)
    return "clang-" + std::to_string(__clang_major__);
#elif defined(__GNUC__)
    return "gcc-" + std::to_string(__GNUC__);
#elif defined(_MSC_VER)
    return "msvc-" + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

// ── §12.1 version document ────────────────────────────────────────────────────

inline std::string version_json() {
    std::string spec(relay::kRelaySpecVersion);
    return std::string("{")
        + "\"tool\":\"cpp-rcp\","
        + "\"protocol\":\"RCP\","
        + "\"protocol_int\":" + std::to_string(static_cast<int>(relay::Protocol::RCP)) + ","
        + "\"version\":\"" + std::string(kVersion) + "\","
        + "\"spec_version\":\"" + spec + "\","
        + "\"language\":\"cpp\","
        + "\"runtime\":\"" + runtime_string() + "\""
        + "}";
}

inline std::string version_text() {
    return std::string("cpp-rcp ") + std::string(kVersion)
        + " (RCP, RELAY spec " + std::string(relay::kRelaySpecVersion)
        + ", cpp, " + runtime_string() + ")";
}

// ── §12.2 capabilities document (always JSON) ────────────────────────────────
// Field set is constrained by cli-capabilities.json (additionalProperties:false).

inline std::string capabilities_json() {
    std::string spec(relay::kRelaySpecVersion);
    return std::string("{")
        + "\"kind\":\"capabilities\","
        + "\"tool\":\"cpp-rcp\","
        + "\"version\":\"" + std::string(kVersion) + "\","
        + "\"spec_version\":\"" + spec + "\","
        + "\"commands\":[\"version\",\"capabilities\",\"status\"],"
        + "\"transports\":[\"mock\",\"sim\",\"shmem\"],"
        + "\"features\":[\"loaning\"],"
        + "\"interfaces\":[\"Node\",\"Caller\"],"
        + "\"optional_interfaces\":[\"LoaningController\"],"
        + "\"adapt\":true"
        + "}";
}

// ── §12.3 status document ────────────────────────────────────────────────────

inline std::string status_json() {
    return std::string("{")
        + "\"protocol\":\"RCP\","
        + "\"tool\":\"cpp-rcp\","
        + "\"version\":\"" + std::string(kVersion) + "\","
        + "\"healthy\":true,"
        + "\"connected\":false,"
        + "\"endpoint\":\"\","
        + "\"details\":{}"
        + "}";
}

inline std::string status_text() {
    return std::string("cpp-rcp: healthy=true connected=false endpoint=");
}

// ── usage ─────────────────────────────────────────────────────────────────────

inline std::string usage() {
    return std::string(
        "Usage: cpp-rcp <command> [--format text|json]\n"
        "Commands:\n"
        "  version       Print tool and spec version\n"
        "  capabilities  Print the RELAY capabilities document (JSON)\n"
        "  status        Print self-assessed health\n");
}

// format_is_json returns true if --format json appears in args (from index 1).
// Returns false for text/absent. Sets *bad to true on an unrecognised --format.
inline bool format_is_json(const std::vector<std::string>& args, bool* bad) {
    *bad = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--format") {
            if (i + 1 >= args.size()) { *bad = true; return false; }
            const std::string& v = args[i + 1];
            if (v == "json") return true;
            if (v == "text") return false;
            *bad = true;
            return false;
        }
    }
    return false; // default: text
}

// run executes the CLI. args[0] is the subcommand (argv without the program name).
// Writes the document to out and diagnostics to err. Returns an exit code (§11.3).
inline int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << usage();
        return kInvalidArgs;
    }
    const std::string& cmd = args[0];

    if (cmd == "version") {
        bool bad = false;
        bool json = format_is_json(args, &bad);
        if (bad) { err << "error: invalid --format\n"; return kInvalidArgs; }
        out << (json ? version_json() : version_text()) << "\n";
        return kOk;
    }
    if (cmd == "capabilities") {
        // Always JSON; no --format flag (§11.1).
        out << capabilities_json() << "\n";
        return kOk;
    }
    if (cmd == "status") {
        bool bad = false;
        bool json = format_is_json(args, &bad);
        if (bad) { err << "error: invalid --format\n"; return kInvalidArgs; }
        out << (json ? status_json() : status_text()) << "\n";
        return kOk; // always healthy → 0 (§11.1: 1 if degraded)
    }
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        out << usage();
        return kOk;
    }

    err << "error: unknown command '" << cmd << "'\n" << usage();
    return kInvalidArgs;
}

} // namespace cli
} // namespace rcp
