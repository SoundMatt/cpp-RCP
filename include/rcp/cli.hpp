// fusa:req REQ-CLI-001
// fusa:req REQ-CLI-002
// fusa:req REQ-CLI-003
// fusa:req REQ-CLI-004
// fusa:req REQ-CLI-005

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
#include <rcp/adapt.hpp>
#include <rcp/mock.hpp>
#include <rcp/version.hpp>

#include <cctype>
#include <cstdint>
#include <istream>
#include <map>
#include <ostream>
#include <sstream>
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
        + "\"protocol\":\"RCP\","
        + "\"protocol_int\":" + std::to_string(static_cast<int>(relay::Protocol::RCP)) + ","
        + "\"version\":\"" + std::string(kVersion) + "\","
        + "\"spec_version\":\"" + spec + "\","
        + "\"commands\":[\"version\",\"capabilities\",\"status\",\"send\"],"
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
        "  version            Print tool and spec version\n"
        "  capabilities       Print the RELAY capabilities document (JSON)\n"
        "  status             Print self-assessed health\n"
        "  send --zone <name> --type <cmdtype> [--payload <hex>] [--format text|json]\n"
        "                     Dispatch a single command to a zone controller (RELAY §11.2)\n"
        "  send --format json Read relay.Message NDJSON on stdin and publish each\n"
        "                     (RELAY §11.2 streaming sink / crossbar spoke)\n");
}

// ── §11.2 streaming JSON sink (crossbar spoke) ───────────────────────────────
//
// A minimal JSON reader, sufficient for one relay.Message per NDJSON line. Only
// the fields rcp::message_to_command() consumes are surfaced (id, payload, seq,
// meta); other fields (protocol, version, timestamp) are accepted and ignored.

// b64_decode decodes standard base64 (padding optional). Returns false on a
// non-base64 character.
inline bool b64_decode(const std::string& in, std::vector<std::uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF)); }
    }
    return true;
}

// hex_decode decodes a plain hex string (even number of [0-9a-fA-F] digits, no
// "0x" prefix or separators) into bytes. Returns false on odd length or a
// non-hex character.
inline bool hex_decode(const std::string& in, std::vector<std::uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    if (in.size() % 2 != 0) return false;
    for (std::size_t i = 0; i < in.size(); i += 2) {
        int hi = val(in[i]);
        int lo = val(in[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

// cmd_type_from_string parses a §11.2 `send --type` flag value (a case-
// insensitive CommandType name) into a CommandType. Returns false for an
// unrecognised name.
inline bool cmd_type_from_string(const std::string& s, CommandType& out) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "noop")     { out = CommandType::Noop;     return true; }
    if (lower == "set")      { out = CommandType::Set;      return true; }
    if (lower == "get")      { out = CommandType::Get;      return true; }
    if (lower == "reset")    { out = CommandType::Reset;    return true; }
    if (lower == "watchdog") { out = CommandType::Watchdog; return true; }
    if (lower == "sleep")    { out = CommandType::Sleep;    return true; }
    if (lower == "wake")     { out = CommandType::Wake;     return true; }
    if (lower == "update")   { out = CommandType::Update;   return true; }
    return false;
}

// JVal is a minimal parsed JSON value.
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool                       b   = false;
    double                     num = 0;
    std::string                str;
    std::vector<JVal>          arr;
    std::map<std::string, JVal> obj;
};

inline void json_skip_ws(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

inline bool json_parse_value(const std::string& s, std::size_t& i, JVal& out);

inline bool json_parse_string(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                int cp = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = s[i++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= h - '0';
                    else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    else return false;
                }
                // Minimal UTF-8 encoding of the BMP code point.
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: return false;
            }
        } else {
            out.push_back(c);
        }
    }
    return false; // unterminated
}

inline bool json_parse_value(const std::string& s, std::size_t& i, JVal& out) {
    json_skip_ws(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { out.type = JVal::Str; return json_parse_string(s, i, out.str); }
    if (c == '{') {
        ++i; out.type = JVal::Obj;
        json_skip_ws(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (i < s.size()) {
            json_skip_ws(s, i);
            std::string key;
            if (!json_parse_string(s, i, key)) return false;
            json_skip_ws(s, i);
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            JVal v;
            if (!json_parse_value(s, i, v)) return false;
            out.obj[key] = std::move(v);
            json_skip_ws(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; return true; }
            return false;
        }
        return false;
    }
    if (c == '[') {
        ++i; out.type = JVal::Arr;
        json_skip_ws(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (i < s.size()) {
            JVal v;
            if (!json_parse_value(s, i, v)) return false;
            out.arr.push_back(std::move(v));
            json_skip_ws(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; return true; }
            return false;
        }
        return false;
    }
    if (c == 't' || c == 'f') {
        if (s.compare(i, 4, "true") == 0)  { i += 4; out.type = JVal::Bool; out.b = true;  return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out.type = JVal::Bool; out.b = false; return true; }
        return false;
    }
    if (c == 'n') {
        if (s.compare(i, 4, "null") == 0) { i += 4; out.type = JVal::Null; return true; }
        return false;
    }
    // number
    std::size_t start = i;
    if (s[i] == '-') ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
    if (i == start) return false;
    out.type = JVal::Num;
    try { out.num = std::stod(s.substr(start, i - start)); }
    catch (...) { return false; }
    return true;
}

// message_from_json parses one relay.Message NDJSON line. Returns false if the
// line is not a JSON object.
inline bool message_from_json(const std::string& line, relay::Message& msg) {
    std::size_t i = 0;
    JVal v;
    if (!json_parse_value(line, i, v) || v.type != JVal::Obj) return false;
    if (auto it = v.obj.find("id"); it != v.obj.end() && it->second.type == JVal::Str)
        msg.id = it->second.str;
    if (auto it = v.obj.find("payload"); it != v.obj.end() && it->second.type == JVal::Str)
        b64_decode(it->second.str, msg.payload);
    if (auto it = v.obj.find("seq"); it != v.obj.end() && it->second.type == JVal::Num)
        msg.seq = static_cast<std::uint64_t>(it->second.num);
    if (auto it = v.obj.find("meta"); it != v.obj.end() && it->second.type == JVal::Obj)
        for (auto& kv : it->second.obj)
            if (kv.second.type == JVal::Str) msg.meta[kv.first] = kv.second.str;
    return true;
}

// send_one implements the §11.2 protocol-flags form of `send` for RCP:
// `--zone <name> --type <cmdtype> [--payload <hex>]`. It dispatches a single
// Command to the matching in-process mock zone controller — the same mock
// backend send_stream uses, since cpp-rcp's CLI has no external transport of
// its own — and reports the result. Exit: 0 sent, 1 error, 2 invalid args.
inline int send_one(const std::vector<std::string>& args, bool json,
                    std::ostream& out, std::ostream& err) {
    std::string zone_flag, type_flag, payload_flag;
    bool have_zone = false, have_type = false, have_payload = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--zone" && i + 1 < args.size()) {
            zone_flag = args[++i];
            have_zone = true;
        } else if (args[i] == "--type" && i + 1 < args.size()) {
            type_flag = args[++i];
            have_type = true;
        } else if (args[i] == "--payload" && i + 1 < args.size()) {
            payload_flag = args[++i];
            have_payload = true;
        } else if (args[i] == "--format") {
            ++i; // already validated by format_is_json
        }
    }
    if (!have_zone || !have_type) {
        err << "usage: cpp-rcp send --zone <name> --type <cmdtype> "
               "[--payload <hex>] [--format text|json]\n";
        return kInvalidArgs;
    }

    CommandType type{};
    if (!cmd_type_from_string(type_flag, type)) {
        err << "error: invalid --type '" << type_flag << "'\n";
        return kInvalidArgs;
    }
    std::vector<std::uint8_t> payload;
    if (have_payload && !hex_decode(payload_flag, payload)) {
        err << "error: invalid --payload hex\n";
        return kInvalidArgs;
    }

    // A zone name that doesn't parse maps to Zone::Unknown, which is not a
    // registered zone either — it fails the same lookup below as a genuinely
    // unregistered zone, matching send_stream's existing not-found handling.
    Zone zone = zone_from_relay_id(zone_flag);

    Command cmd;
    cmd.zone    = zone;
    cmd.type    = type;
    cmd.payload = payload;

    mock::Registry reg;
    std::shared_ptr<Controller> ctrl;
    if (reg.lookup(zone, ctrl)) {
        err << "send: zone '" << zone_flag << "' not found\n";
        return kError;
    }
    Response resp;
    if (ctrl->send(Context{}, cmd, resp)) {
        err << "send: zone " << to_string(zone) << ": send failed\n";
        return kError;
    }

    if (json) {
        out << "{\"sent\":true,\"zone\":\"" << zone_to_relay_id(zone)
            << "\",\"status\":\"" << to_string(resp.status) << "\"}\n";
    } else {
        out << "sent to " << zone_to_relay_id(zone) << " (" << to_string(resp.status) << ")\n";
    }
    return kOk;
}

// send_stream implements `send --format json` (RELAY §11.2): read relay.Message
// values as NDJSON on `in`, publish each via message_to_command → the matching
// mock zone controller until EOF. Malformed/undeliverable lines are reported and
// skipped (a single bad message must not tear down a crossbar route); only the
// final count is written to `out`. Exit code is always kOk on clean EOF.
inline int send_stream(std::istream& in, std::ostream& out, std::ostream& err) {
    mock::Registry reg;
    std::string line;
    std::size_t sent = 0;
    while (std::getline(in, line)) {
        // trim trailing CR / surrounding whitespace
        std::size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        std::size_t e = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(b, e - b + 1);

        relay::Message msg;
        if (!message_from_json(trimmed, msg)) {
            err << "send: skipping malformed message\n";
            continue;
        }
        Command cmd = message_to_command(msg);
        std::shared_ptr<Controller> ctrl;
        if (reg.lookup(cmd.zone, ctrl)) {
            err << "send: zone " << to_string(cmd.zone) << " not found\n";
            continue;
        }
        Response resp;
        if (ctrl->send(Context{}, cmd, resp)) {
            err << "send: zone " << to_string(cmd.zone) << ": send failed\n";
            continue;
        }
        ++sent;
    }
    out << "published " << sent << " message(s)\n";
    return kOk;
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
// `in` supplies stdin for streaming commands (send --format json). Writes the
// document to out and diagnostics to err. Returns an exit code (§11.3).
inline int run(const std::vector<std::string>& args, std::istream& in,
               std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << usage();
        return kInvalidArgs;
    }
    const std::string& cmd = args[0];

    if (cmd == "send") {
        bool bad = false;
        bool json = format_is_json(args, &bad);
        if (bad) { err << "error: invalid --format\n"; return kInvalidArgs; }

        bool has_protocol_flags = false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--zone" || args[i] == "--type" || args[i] == "--payload") {
                has_protocol_flags = true;
                break;
            }
        }
        if (has_protocol_flags) {
            // §11.2 protocol-flags form: --zone <name> --type <cmdtype> [--payload <hex>].
            return send_one(args, json, out, err);
        }
        if (!json) {
            err << "usage: cpp-rcp send --format json  (NDJSON relay.Message on stdin)\n"
                   "   or: cpp-rcp send --zone <name> --type <cmdtype> [--payload <hex>] "
                   "[--format text|json]\n";
            return kInvalidArgs;
        }
        // §11.2 streaming JSON sink form (crossbar spoke).
        return send_stream(in, out, err);
    }

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

// run overload without stdin — for the non-streaming commands and unit tests.
inline int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::istringstream empty;
    return run(args, empty, out, err);
}

} // namespace cli
} // namespace rcp
