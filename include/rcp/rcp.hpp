// fusa:req REQ-ZONE-001
// fusa:req REQ-ZONE-002
// fusa:req REQ-ZONE-003
// fusa:req REQ-ZONE-004
// fusa:req REQ-ZONE-005
// fusa:req REQ-ZONE-006
// fusa:req REQ-ZONE-007
// fusa:req REQ-ZONE-008
// fusa:req REQ-PRI-001
// fusa:req REQ-PRI-002
// fusa:req REQ-PRI-003
// fusa:req REQ-CMD-001
// fusa:req REQ-CMD-002
// fusa:req REQ-CMD-003
// fusa:req REQ-CMD-004
// fusa:req REQ-CMD-005
// fusa:req REQ-CMD-006
// fusa:req REQ-STATUS-001
// fusa:req REQ-STATUS-002
// fusa:req REQ-STATUS-003
// fusa:req REQ-STATUS-004
// fusa:req REQ-STATUS-005
// fusa:req REQ-STATUS-006
// fusa:req REQ-ERR-001
// fusa:req REQ-ERR-002
// fusa:req REQ-ERR-003
// fusa:req REQ-ERR-004
// fusa:req REQ-ERR-005
// fusa:req REQ-ERR-006
// fusa:req REQ-ERR-007
// fusa:req REQ-ERR-008
// fusa:req REQ-ERR-009
// fusa:req REQ-ERR-010
// fusa:req REQ-ERR-011
// fusa:req REQ-CMDSTRUCT-001
// fusa:req REQ-CMDSTRUCT-002
// fusa:req REQ-RESP-001
// fusa:req REQ-RESP-002
// fusa:req REQ-RESP-003
// fusa:req REQ-STAT-001
// fusa:req REQ-STAT-002
// fusa:req REQ-STAT-003
// fusa:req REQ-STAT-004
// fusa:req REQ-STAT-005

// Package rcp provides the Remote Control Protocol for automotive zonal architecture.
//
// A central high-performance computer uses a Registry to discover zone controllers,
// then dispatches Commands to each Controller and receives Responses and Status
// telemetry in return.
//
// RELAY conformance: include <relay/relay.hpp> for relay:: namespace types, and
// <rcp/adapt.hpp> for Adapt() which wraps a Controller as a relay::Caller.
#pragma once

#include <relay/relay.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {

// ── Error codes ───────────────────────────────────────────────────────────────

enum class Errc : int {
    closed         = 1,
    not_found      = 2,
    already_exists = 3,
    timeout        = 4,
    busy           = 5,
    zone_mismatch  = 6,
};

inline const std::error_category& rcp_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp"; }
        std::string message(int ev) const override {
            switch (static_cast<Errc>(ev)) {
            case Errc::closed:         return "rcp: controller closed";
            case Errc::not_found:      return "rcp: zone not found";
            case Errc::already_exists: return "rcp: zone already registered";
            case Errc::timeout:        return "rcp: command timeout";
            case Errc::busy:           return "rcp: zone controller busy";
            case Errc::zone_mismatch:  return "rcp: zone mismatch";
            default:                   return "rcp: unknown error";
            }
        }
        // Map rcp::Errc codes to relay::Errc conditions (§5.2, §5.3).
        bool equivalent(int code, const std::error_condition& cond) const noexcept override {
            if (cond.category() != relay::relay_category()) return false;
            auto re = static_cast<relay::Errc>(cond.value());
            switch (static_cast<Errc>(code)) {
            case Errc::closed:         return re == relay::Errc::closed;
            case Errc::timeout:        return re == relay::Errc::timeout;
            case Errc::busy:           return re == relay::Errc::timeout;
            case Errc::not_found:      return re == relay::Errc::not_connected;
            case Errc::zone_mismatch:  return re == relay::Errc::not_connected;
            case Errc::already_exists: return false;  // standalone per §5.4 (RELAY spec updated)
            default:                   return false;
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(Errc e) noexcept {
    return {static_cast<int>(e), rcp_category()};
}

// Sentinel error codes — analogous to Go's package-level sentinel errors.
inline const std::error_code ErrClosed        = make_error_code(Errc::closed);
inline const std::error_code ErrNotFound      = make_error_code(Errc::not_found);
inline const std::error_code ErrAlreadyExists = make_error_code(Errc::already_exists);
inline const std::error_code ErrTimeout       = make_error_code(Errc::timeout);
inline const std::error_code ErrBusy          = make_error_code(Errc::busy);
inline const std::error_code ErrZoneMismatch  = make_error_code(Errc::zone_mismatch);

// ── Zone ─────────────────────────────────────────────────────────────────────

enum class Zone : uint8_t {
    Unknown    = 0,
    FrontLeft  = 1,
    FrontRight = 2,
    RearLeft   = 3,
    RearRight  = 4,
    Central    = 5,
};

inline std::string to_string(Zone z) {
    switch (z) {
    case Zone::FrontLeft:  return "front-left";
    case Zone::FrontRight: return "front-right";
    case Zone::RearLeft:   return "rear-left";
    case Zone::RearRight:  return "rear-right";
    case Zone::Central:    return "central";
    default:               return "unknown";
    }
}

// ── Priority ──────────────────────────────────────────────────────────────────

enum class Priority : uint8_t {
    Normal   = 0,
    High     = 1,
    Critical = 2,
};

// ── CommandType ───────────────────────────────────────────────────────────────

enum class CommandType : uint16_t {
    Noop     = 0,
    Set      = 1,
    Get      = 2,
    Reset    = 3,
    Watchdog = 4,
    Sleep    = 5,
    Wake     = 6,
    Update   = 7, // OTA firmware update session (M20)
};

// ── ResponseStatus ────────────────────────────────────────────────────────────

enum class ResponseStatus : uint8_t {
    OK      = 0,
    Error   = 1,
    Timeout = 2,
    Busy    = 3,
    Unknown = 4,
};

inline std::string to_string(ResponseStatus s) {
    switch (s) {
    case ResponseStatus::OK:      return "OK";
    case ResponseStatus::Error:   return "error";
    case ResponseStatus::Timeout: return "timeout";
    case ResponseStatus::Busy:    return "busy";
    default:                      return "unknown";
    }
}

// ── Data structures ───────────────────────────────────────────────────────────

struct Command {
    uint32_t             id       = 0;
    Zone                 zone     = Zone::Unknown;
    CommandType          type     = CommandType::Noop;
    Priority             priority = Priority::Normal;
    std::vector<uint8_t> payload;
};

struct Response {
    uint32_t             command_id = 0;
    Zone                 zone       = Zone::Unknown;
    ResponseStatus       status     = ResponseStatus::OK;
    std::vector<uint8_t> payload;
};

struct Status {
    Zone                 zone    = Zone::Unknown;
    uint32_t             seq     = 0;
    bool                 healthy = false;
    std::vector<uint8_t> payload;
};

// Loan is a payload buffer borrowed from a LoaningController's pool.
// The caller MUST either pass it to LoaningController::send_loaned (transferring
// ownership) or let it go out of scope (which calls the release function).
class Loan {
public:
    std::vector<uint8_t> payload;

    Loan() = default;
    Loan(std::vector<uint8_t> p, std::function<void()> r)
        : payload(std::move(p)), release_(std::move(r)) {}

    ~Loan() { if (release_) release_(); }

    Loan(const Loan&) = delete;
    Loan& operator=(const Loan&) = delete;

    Loan(Loan&& o) noexcept
        : payload(std::move(o.payload)), release_(std::move(o.release_)) {
        o.release_ = nullptr;
    }
    Loan& operator=(Loan&&) = delete;

    // Return releases the Loan back to the pool without sending.
    void ret() {
        if (release_) { release_(); release_ = nullptr; }
    }

private:
    std::function<void()> release_;
};

// ── Context — relay::Context alias (§18.2) ────────────────────────────────────
// rcp::Context is an alias for relay::Context per §18.2 conformance.

using Context = relay::Context;

// ── StatusChannel — relay::Channel<Status> alias (§18.2) ─────────────────────
// rcp::StatusChannel is an alias for relay::Channel<Status>.
// Legacy code using std::shared_ptr<StatusChannel> is unchanged.

using StatusChannel = relay::Channel<Status>;

// ── Controller ────────────────────────────────────────────────────────────────

class Controller {
public:
    virtual ~Controller() = default;

    virtual Zone zone() const noexcept = 0;

    // Send dispatches a command and waits for the response.
    // Returns ErrClosed if the controller has been closed.
    // Returns ErrTimeout if ctx is done before a response arrives.
    // Returns ErrZoneMismatch if cmd.zone != this->zone().
    virtual std::error_code send(const Context& ctx,
                                  const Command& cmd,
                                  Response&      out) = 0;

    // Subscribe returns a channel of periodic Status updates.
    // The channel is closed when ctx becomes done or the controller closes.
    // Returns ErrClosed if the controller is already closed.
    virtual std::error_code subscribe(const Context&                ctx,
                                       std::shared_ptr<StatusChannel>& out) = 0;

    // Close releases all resources. Safe to call multiple times.
    virtual std::error_code close() = 0;
};

// ── LoaningController ─────────────────────────────────────────────────────────

class LoaningController : public Controller {
public:
    // Loan returns a zeroed payload buffer of exactly size bytes.
    // Returns ErrClosed if the controller is closed.
    // Returns an error if size < 0.
    virtual std::error_code loan(int size, std::unique_ptr<Loan>& out) = 0;

    // SendLoaned sends cmd whose payload is the buffer from a prior loan() call.
    // Ownership of cmd.payload transfers to the transport on return.
    virtual std::error_code send_loaned(const Context& ctx,
                                         Command&       cmd,
                                         Response&      out) = 0;
};

// ── Registry ─────────────────────────────────────────────────────────────────

class Registry {
public:
    virtual ~Registry() = default;

    // Register adds a controller.
    // Returns ErrAlreadyExists if a controller for the same zone is registered.
    // Returns ErrClosed if the registry is closed.
    virtual std::error_code register_ctrl(std::shared_ptr<Controller> ctrl) = 0;

    // Deregister removes and closes the controller for the given zone.
    // Returns ErrNotFound if the zone is not registered.
    virtual std::error_code deregister(Zone zone) = 0;

    // Lookup returns the controller for the given zone.
    // Returns ErrNotFound if no controller is registered.
    // Returns ErrClosed if the registry is closed.
    virtual std::error_code lookup(Zone                        zone,
                                    std::shared_ptr<Controller>& out) = 0;

    // Controllers returns all currently registered controllers.
    virtual std::vector<std::shared_ptr<Controller>> controllers() = 0;

    // Close closes all registered controllers and releases registry resources.
    // Safe to call multiple times.
    virtual std::error_code close() = 0;
};

} // namespace rcp

// Enable std::error_code construction from rcp::Errc.
namespace std {
template <>
struct is_error_code_enum<rcp::Errc> : true_type {};
} // namespace std
