// fusa:req REQ-RELAY-001
// fusa:req REQ-RELAY-002
// fusa:req REQ-RELAY-003
// fusa:req REQ-RELAY-004
// fusa:req REQ-RELAY-005

// RELAY application interface adapter for cpp-RCP (§10.3, §18.2).
//
// Adapt() wraps an rcp::Controller as a relay::Caller so application code can
// use the protocol-agnostic relay::Node / relay::Caller interface and swap the
// underlying protocol with a single constructor change.
//
// Usage:
//   auto ctrl = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
//   auto caller = rcp::Adapt(ctrl);          // relay::Caller*
//   auto [resp, ec] = caller->call(ctx, msg);
#pragma once

#include <rcp/rcp.hpp>
#include <relay/relay.hpp>

#include <charconv>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace rcp {

// ── Zone ↔ relay::Message.ID helpers (§15.7.5) ───────────────────────────────

// zone_to_relay_id returns the RELAY-canonical PascalCase zone name (§4.2).
inline std::string zone_to_relay_id(Zone z) {
    switch (z) {
    case Zone::FrontLeft:  return "FrontLeft";
    case Zone::FrontRight: return "FrontRight";
    case Zone::RearLeft:   return "RearLeft";
    case Zone::RearRight:  return "RearRight";
    case Zone::Central:    return "Central";
    default:               return "Unknown";
    }
}

// zone_from_relay_id parses a PascalCase zone name from relay::Message.ID.
inline Zone zone_from_relay_id(const std::string& id) {
    if (id == "FrontLeft")  return Zone::FrontLeft;
    if (id == "FrontRight") return Zone::FrontRight;
    if (id == "RearLeft")   return Zone::RearLeft;
    if (id == "RearRight")  return Zone::RearRight;
    if (id == "Central")    return Zone::Central;
    return Zone::Unknown;
}

// priority_from_meta parses the "rcp.priority" meta key.
inline Priority priority_from_meta(const std::map<std::string, std::string>& meta) {
    auto it = meta.find("rcp.priority");
    if (it == meta.end()) return Priority::Normal;
    if (it->second == "high")     return Priority::High;
    if (it->second == "critical") return Priority::Critical;
    return Priority::Normal;
}

// cmd_type_from_meta parses the "rcp.cmd_type" meta key.
inline CommandType cmd_type_from_meta(const std::map<std::string, std::string>& meta) {
    auto it = meta.find("rcp.cmd_type");
    if (it == meta.end()) return CommandType::Noop;
    const auto& s = it->second;
    if (s == "set")      return CommandType::Set;
    if (s == "get")      return CommandType::Get;
    if (s == "reset")    return CommandType::Reset;
    if (s == "watchdog") return CommandType::Watchdog;
    if (s == "sleep")    return CommandType::Sleep;
    if (s == "wake")     return CommandType::Wake;
    return CommandType::Noop;
}

// ── ToMessage / FromMessage (§15.7.5) ────────────────────────────────────────

// status_to_message converts rcp::Status to relay::Message (subscribe direction).
inline relay::Message status_to_message(const Status& s) {
    relay::Message msg;
    msg.protocol    = relay::Protocol::RCP;
    msg.id          = zone_to_relay_id(s.zone);
    msg.payload     = s.payload;
    msg.seq         = s.seq;
    msg.timestamp   = std::chrono::system_clock::now();
    msg.meta["rcp.healthy"] = s.healthy ? "true" : "false";
    return msg;
}

// response_to_message converts rcp::Response to relay::Message (call direction).
inline relay::Message response_to_message(const Response& r) {
    relay::Message msg;
    msg.protocol  = relay::Protocol::RCP;
    msg.id        = zone_to_relay_id(r.zone);
    msg.payload   = r.payload;
    msg.timestamp = std::chrono::system_clock::now();
    msg.meta["rcp.status"] = std::to_string(static_cast<int>(r.status));
    return msg;
}

// message_to_command converts a relay::Message to rcp::Command (call direction).
inline Command message_to_command(const relay::Message& msg) {
    Command cmd;
    cmd.zone     = zone_from_relay_id(msg.id);
    cmd.priority = priority_from_meta(msg.meta);
    cmd.type     = cmd_type_from_meta(msg.meta);
    cmd.payload  = msg.payload;
    return cmd;
}

// ── RcpCallerAdapter — implements relay::Caller over rcp::Controller ──────────

class RcpCallerAdapter final : public relay::Caller {
public:
    explicit RcpCallerAdapter(std::shared_ptr<Controller> ctrl)
        : ctrl_(std::move(ctrl)) {}

    relay::Protocol protocol() const noexcept override {
        return relay::Protocol::RCP;
    }

    // send maps relay::Message → rcp::Command, discards the response (§10.6).
    std::error_code send(relay::Context ctx, const relay::Message& msg) override {
        if (!ctrl_) return make_error_code(Errc::not_found);
        Command cmd = message_to_command(msg);
        cmd.zone    = zone_from_relay_id(msg.id);
        Response resp;
        return ctrl_->send(ctx, cmd, resp);
    }

    // call maps relay::Message → rcp::Command → relay::Message (§10.2).
    std::pair<relay::Message, std::error_code>
        call(relay::Context ctx, const relay::Message& req) override {
        if (!ctrl_) return {{}, make_error_code(Errc::not_found)};
        Command cmd = message_to_command(req);
        Response resp;
        auto ec = ctrl_->send(ctx, cmd, resp);
        if (ec) return {{}, ec};
        return {response_to_message(resp), {}};
    }

    // subscribe wraps rcp::Controller::subscribe, forwarding Status as relay::Message.
    // Starts a background thread per §10.5 that reads from StatusChannel and pushes
    // to the relay::Channel<Message>. Thread exits when the status channel closes.
    std::pair<std::shared_ptr<relay::Channel<relay::Message>>, std::error_code>
        subscribe(relay::SubscriberOptions opts = {}) override {
        if (!ctrl_) return {nullptr, make_error_code(Errc::not_found)};

        std::shared_ptr<StatusChannel> status_ch;
        auto ec = ctrl_->subscribe(relay::Context::background(), status_ch);
        if (ec) return {nullptr, ec};

        auto out = std::make_shared<relay::Channel<relay::Message>>(opts.channel_depth);
        auto bp  = opts.back_pressure;

        std::thread([status_ch, out, bp]() mutable {
            while (true) {
                auto st = status_ch->recv();
                if (!st) break;
                auto msg = status_to_message(*st);
                switch (bp) {
                case relay::BackPressurePolicy::drop_newest:
                    out->push(std::move(msg));
                    break;
                case relay::BackPressurePolicy::drop_oldest:
                    if (!out->push(msg)) {
                        out->try_recv();
                        out->push(std::move(msg));
                    }
                    break;
                case relay::BackPressurePolicy::block:
                    while (!out->push(msg) && !out->is_closed()) {
                        // spin until space available or channel closed
                    }
                    break;
                }
            }
            out->close();
        }).detach();

        return {out, {}};
    }

    std::error_code close() noexcept override {
        if (!ctrl_) return {};
        return ctrl_->close();
    }

private:
    std::shared_ptr<Controller> ctrl_;
};

// ── Adapt() (§10.3) ──────────────────────────────────────────────────────────

// Adapt wraps an rcp::Controller as a relay::Caller.
// The returned relay::Caller also satisfies relay::Node.
// Does NOT block or connect; wraps the given controller immediately.
inline std::unique_ptr<relay::Caller> Adapt(std::shared_ptr<Controller> ctrl) {
    return std::make_unique<RcpCallerAdapter>(std::move(ctrl));
}

} // namespace rcp
