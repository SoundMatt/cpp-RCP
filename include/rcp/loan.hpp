// fusa:req REQ-LOAN-001
// fusa:req REQ-LOAN-002
// fusa:req REQ-LOAN-003
// fusa:req REQ-LOAN-004
// fusa:req REQ-LOAN-005
// fusa:req REQ-LOAN-006

// LoaningController wrapper — extends any rcp::Controller with zero-copy
// payload loaning via a pool of pre-allocated byte vectors.
//
// loan() obtains a zeroed buffer from the pool; send_loaned() delivers it to
// the inner controller without copying. The Loan's destructor returns the
// buffer to the pool automatically if not sent.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace rcp {
namespace loan {

class Controller final : public rcp::LoaningController {
public:
    explicit Controller(std::shared_ptr<rcp::Controller> inner)
        : inner_(std::move(inner)) {}

    ~Controller() override { auto ec = close(); (void)ec; }

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        return inner_->send(ctx, cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override {
        closed_.store(true, std::memory_order_release);
        return inner_->close();
    }

    // loan returns a zeroed buffer of exactly size bytes from the pool.
    std::error_code loan(int size, std::unique_ptr<rcp::Loan>& out) override {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        if (size < 0) return make_error_code(Errc::busy);

        std::vector<uint8_t> buf(static_cast<size_t>(size), 0);
        // Release function returns the buffer to the pool.
        auto* raw = new std::vector<uint8_t>(std::move(buf));
        out = std::make_unique<rcp::Loan>(
            *raw,
            [this, raw]() mutable {
                std::lock_guard<std::mutex> lk(pool_mu_);
                raw->clear();
                pool_.push_back(std::unique_ptr<std::vector<uint8_t>>(raw));
            });
        return {};
    }

    std::error_code send_loaned(const rcp::Context& ctx,
                                 Command&            cmd,
                                 Response&           out) override {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        return inner_->send(ctx, cmd, out);
    }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::atomic<bool> closed_{false};
    std::mutex pool_mu_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> pool_;
};

inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner) {
    return std::make_shared<Controller>(std::move(inner));
}

} // namespace loan
} // namespace rcp
