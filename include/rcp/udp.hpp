// fusa:req REQ-UDP-001
// fusa:req REQ-UDP-002
// fusa:req REQ-UDP-003
// fusa:req REQ-UDP-004
// fusa:req REQ-UDP-005
// fusa:req REQ-UDP-006
// fusa:req REQ-UDP-007
// fusa:req REQ-UDP-008
// fusa:req REQ-UDP-009
// fusa:req REQ-UDP-010
// fusa:req REQ-UDP-011
// fusa:req REQ-UDP-012

// Pure-C++ UDP transport for the RCP protocol.
//
// On POSIX (Linux, macOS): full implementation using BSD sockets.
// On Windows: stub that returns std::errc::function_not_supported.
//
// Frame format is defined in rcp/wire.hpp.
#pragma once

#include "rcp.hpp"
#include "wire.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define RCP_UDP_POSIX 1
#endif

namespace rcp {
namespace udp {

#if defined(RCP_UDP_POSIX)

// ── ZoneServer ────────────────────────────────────────────────────────────────

// ZoneServer listens on a UDP port, handles Command frames, and publishes
// Status to all registered subscribers.
class ZoneServer {
public:
    using Handler = std::function<Response(const Command&)>;

    ZoneServer(Zone zone, const char* addr, uint16_t port)
        : zone_(zone), fd_(-1) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(port);
        if (!addr || addr[0] == '\0')
            sa.sin_addr.s_addr = INADDR_ANY;
        else
            ::inet_pton(AF_INET, addr, &sa.sin_addr);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        healthy_.store(true);
        serve_thread_ = std::thread([this]{ serve(); });
    }

    ~ZoneServer() { close(); }

    // addr_string returns "host:port" for this server's bound address.
    std::string addr_string() const {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        if (fd_ < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len) < 0)
            return {};
        char buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(sa.sin_port));
    }

    uint16_t port() const {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        if (fd_ < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len) < 0)
            return 0;
        return ntohs(sa.sin_port);
    }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
    }

    void set_healthy(bool v) { healthy_.store(v, std::memory_order_release); }

    void publish(const std::vector<uint8_t>& payload) {
        uint32_t seq = ++seq_;
        Status st{zone_, seq, healthy_.load(), payload};
        auto frame = wire::encode_status(st);
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& kv : subscribers_) {
            ::sendto(fd_, frame.data(), frame.size(), 0,
                     reinterpret_cast<const sockaddr*>(&kv.second), sizeof(kv.second));
        }
    }

    void close() {
        if (!closed_.exchange(true)) {
            if (fd_ >= 0) {
                ::shutdown(fd_, SHUT_RDWR);
                ::close(fd_);
                fd_ = -1;
            }
        }
        if (serve_thread_.joinable()) serve_thread_.join();
    }

    bool ok() const noexcept { return fd_ >= 0; }

private:
    Zone   zone_;
    int    fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<bool>     healthy_{false};
    std::atomic<uint32_t> seq_{0};
    std::mutex   mu_;
    Handler      handler_;
    std::map<std::string, sockaddr_in> subscribers_;
    std::thread  serve_thread_;

    static std::string addr_key(const sockaddr_in& sa) {
        char buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(sa.sin_port));
    }

    void serve() {
        std::vector<uint8_t> buf(wire::HeaderLen + wire::MaxPayload);
        sockaddr_in client{};
        socklen_t   clen = sizeof(client);
        while (!closed_.load()) {
            ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                                    reinterpret_cast<sockaddr*>(&client), &clen);
            if (n <= 0) break;
            if (static_cast<size_t>(n) < wire::HeaderLen) continue;
            switch (buf[3]) {
            case wire::TypeCommand: {
                Command cmd;
                if (wire::decode_command(buf.data(), static_cast<size_t>(n), cmd)) break;
                Response resp;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (handler_) resp = handler_(cmd);
                    else          resp = Response{cmd.id, zone_, ResponseStatus::OK, {}};
                }
                auto frame = wire::encode_response(resp);
                ::sendto(fd_, frame.data(), frame.size(), 0,
                         reinterpret_cast<sockaddr*>(&client), clen);
                break;
            }
            case wire::TypeSubscribe:
                subscribers_[addr_key(client)] = client;
                break;
            case wire::TypeUnsubscribe:
                subscribers_.erase(addr_key(client));
                break;
            default:
                break;
            }
        }
    }
};

// ── Controller ────────────────────────────────────────────────────────────────

// Controller connects to a ZoneServer via UDP and implements rcp::Controller.
class Controller final : public rcp::Controller {
public:
    // Connect to serverHost:serverPort for zone.
    Controller(Zone zone, const char* server_host, uint16_t server_port)
        : zone_(zone), fd_(-1) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(server_port);
        ::inet_pton(AF_INET, server_host, &sa.sin_addr);

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        read_thread_ = std::thread([this]{ read_loop(); });
    }

    ~Controller() override { auto ec = close(); (void)ec; }

    Zone zone() const noexcept override { return zone_; }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load()) return ErrClosed;
        if (ctx.done())     return ErrTimeout;
        if (cmd.zone != zone_) return ErrZoneMismatch;

        uint32_t id = ++next_id_;
        Command safe = cmd;
        if (!cmd.payload.empty()) safe.payload = cmd.payload;
        safe.id = id;

        auto frame  = wire::encode_command(safe);
        auto result = std::make_shared<std::promise<Response>>();
        auto future = result->get_future();
        {
            std::lock_guard<std::mutex> lk(mu_);
            pending_[id] = result;
        }

        auto cleanup = [&]{
            std::lock_guard<std::mutex> lk(mu_);
            pending_.erase(id);
        };

        if (::send(fd_, frame.data(), frame.size(), 0) < 0) {
            cleanup();
            return ErrClosed;
        }

        // Wait for response or timeout.
        std::future_status st;
        if (ctx.deadline()) {
            st = future.wait_until(*ctx.deadline());
        } else {
            future.wait();
            st = std::future_status::ready;
        }
        cleanup();
        if (st == std::future_status::timeout) return ErrTimeout;
        if (!future.valid()) return ErrClosed;
        out = future.get();
        return {};
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        if (closed_.load()) return ErrClosed;

        auto ch = std::make_shared<StatusChannel>(16);
        {
            std::lock_guard<std::mutex> lk(mu_);
            subs_.push_back(ch);
        }

        // Send subscribe control frame.
        auto frame = wire::encode_control(wire::TypeSubscribe, zone_);
        ::send(fd_, frame.data(), frame.size(), 0);

        std::thread([this, weak_ch = std::weak_ptr<StatusChannel>(ch), ctx]() mutable {
            while (!ctx.done() && !closed_.load()) {
                auto c = weak_ch.lock();
                if (!c || c->is_closed()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // Send unsubscribe.
            if (!closed_.load()) {
                auto frame2 = wire::encode_control(wire::TypeUnsubscribe, zone_);
                ::send(fd_, frame2.data(), frame2.size(), 0);
            }
            auto c = weak_ch.lock();
            if (!c) return;
            {
                std::lock_guard<std::mutex> lk(mu_);
                auto it = std::find(subs_.begin(), subs_.end(), c);
                if (it != subs_.end()) subs_.erase(it);
            }
            c->close();
        }).detach();

        out = std::move(ch);
        return {};
    }

    std::error_code close() override {
        if (!closed_.exchange(true)) {
            if (fd_ >= 0) {
                ::shutdown(fd_, SHUT_RDWR);
                ::close(fd_);
            }
        }
        if (read_thread_.joinable()) read_thread_.join();
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_) s->close();
        subs_.clear();
        return {};
    }

    bool ok() const noexcept { return fd_ >= 0; }

private:
    Zone  zone_;
    int   fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint32_t> next_id_{0};
    std::mutex mu_;
    std::map<uint32_t, std::shared_ptr<std::promise<Response>>> pending_;
    std::vector<std::shared_ptr<StatusChannel>> subs_;
    std::thread read_thread_;

    void read_loop() {
        std::vector<uint8_t> buf(wire::HeaderLen + wire::MaxPayload);
        while (!closed_.load()) {
            ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            if (static_cast<size_t>(n) < wire::HeaderLen) continue;
            switch (buf[3]) {
            case wire::TypeResponse: {
                Response resp;
                if (wire::decode_response(buf.data(), static_cast<size_t>(n), resp)) break;
                std::lock_guard<std::mutex> lk(mu_);
                auto it = pending_.find(resp.command_id);
                if (it != pending_.end()) {
                    it->second->set_value(std::move(resp));
                }
                break;
            }
            case wire::TypeStatus: {
                Status st;
                if (wire::decode_status(buf.data(), static_cast<size_t>(n), st)) break;
                std::lock_guard<std::mutex> lk(mu_);
                for (auto& s : subs_) s->push(st);
                break;
            }
            default:
                break;
            }
        }
    }
};

// ── Registry ──────────────────────────────────────────────────────────────────

class Registry final : public rcp::Registry {
public:
    std::error_code dial(Zone z, const char* host, uint16_t port) {
        auto ctrl = std::make_shared<Controller>(z, host, port);
        if (!ctrl->ok()) return make_error_code(Errc::not_found);
        return register_ctrl(ctrl);
    }

    std::error_code register_ctrl(std::shared_ptr<rcp::Controller> ctrl) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        if (ctrls_.count(ctrl->zone())) return ErrAlreadyExists;
        ctrls_[ctrl->zone()] = ctrl;
        return {};
    }

    std::error_code deregister(Zone z) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        auto ctrl = it->second;
        ctrls_.erase(it);
        lk.unlock();
        return ctrl->close();
    }

    std::error_code lookup(Zone z, std::shared_ptr<rcp::Controller>& out) override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        out = it->second;
        return {};
    }

    std::vector<std::shared_ptr<rcp::Controller>> controllers() override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        std::vector<std::shared_ptr<rcp::Controller>> out;
        out.reserve(ctrls_.size());
        for (auto& kv : ctrls_) out.push_back(kv.second);
        return out;
    }

    std::error_code close() override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        auto local = std::move(ctrls_);
        lk.unlock();
        for (auto& kv : local) { auto ec = kv.second->close(); (void)ec; }
        return {};
    }

private:
    mutable std::shared_mutex mu_;
    std::map<Zone, std::shared_ptr<rcp::Controller>> ctrls_;
    bool closed_ = false;
};

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

#else // !RCP_UDP_POSIX (Windows stub)

class ZoneServer {
public:
    ZoneServer(Zone, const char*, uint16_t) {}
    std::string addr_string() const { return {}; }
    uint16_t    port()        const { return 0; }
    void set_handler(std::function<Response(const Command&)>) {}
    void set_healthy(bool) {}
    void publish(const std::vector<uint8_t>&) {}
    void close() {}
    bool ok() const noexcept { return false; }
};

class Controller final : public rcp::Controller {
public:
    Controller(Zone z, const char*, uint16_t) : zone_(z) {}
    Zone zone() const noexcept override { return zone_; }
    std::error_code send(const rcp::Context&, const Command&, Response&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code subscribe(const rcp::Context&, std::shared_ptr<StatusChannel>&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code close() override { return {}; }
    bool ok() const noexcept { return false; }
private:
    Zone zone_;
};

class Registry final : public rcp::Registry {
public:
    std::error_code register_ctrl(std::shared_ptr<rcp::Controller>) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code deregister(Zone) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code lookup(Zone, std::shared_ptr<rcp::Controller>&) override {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::vector<std::shared_ptr<rcp::Controller>> controllers() override { return {}; }
    std::error_code close() override { return {}; }
};

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

#endif // RCP_UDP_POSIX

} // namespace udp
} // namespace rcp
