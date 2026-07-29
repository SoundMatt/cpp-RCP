// fusa:test REQ-CFG-001
// fusa:test REQ-CFG-002
// fusa:test REQ-CFG-003
// fusa:test REQ-CFG-004
// fusa:test REQ-CFG-005
// fusa:test REQ-CFG-006
#include <catch2/catch_test_macros.hpp>

#include "rcp/config.hpp"
#include "rcp/legacy_mock.hpp"

#include <map>
#include <stdexcept>
#include <string>

using namespace rcp;

namespace {

// A minimal empty-starting rcp::Registry test double.
//
// legacy_mock::Registry always pre-populates all 5 zones (see its own
// constructor), so it can't be used to observe "did config::load actually
// register this zone" starting from a clean slate. Milestone 61's
// deprecation sweep (ROADMAP.md, v2.17.0) removed rcp/proxy.hpp, which
// these tests previously borrowed purely as a generic empty rcp::Registry
// implementation — no proxy-specific behavior was ever under test here.
// This local double replaces that borrowed dependency directly instead of
// pulling proxy.hpp's concept back in; rcp/config.hpp itself is otherwise
// unchanged, since its own rebind onto the new server/endpoint model
// remains a separate, still-open item (see the Satellite Package
// Disposition table's `config.hpp` entry).
class EmptyRegistry final : public rcp::Registry {
public:
    std::error_code register_ctrl(std::shared_ptr<rcp::Controller> ctrl) override {
        if (closed_) return ErrClosed;
        if (ctrls_.count(ctrl->zone())) return ErrAlreadyExists;
        ctrls_[ctrl->zone()] = std::move(ctrl);
        return {};
    }

    std::error_code deregister(Zone z) override {
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        auto ctrl = it->second;
        ctrls_.erase(it);
        return ctrl->close();
    }

    std::error_code lookup(Zone z, std::shared_ptr<rcp::Controller>& out) override {
        if (closed_) return ErrClosed;
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        out = it->second;
        return {};
    }

    std::vector<std::shared_ptr<rcp::Controller>> controllers() override {
        std::vector<std::shared_ptr<rcp::Controller>> out;
        out.reserve(ctrls_.size());
        for (auto& kv : ctrls_) out.push_back(kv.second);
        return out;
    }

    std::error_code close() override {
        if (closed_) return {};
        closed_ = true;
        auto local = std::move(ctrls_);
        for (auto& kv : local) (void)kv.second->close();
        return {};
    }

private:
    std::map<Zone, std::shared_ptr<rcp::Controller>> ctrls_;
    bool closed_ = false;
};

} // namespace

TEST_CASE("config: parse_json two zones", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "FrontLeft",  "priority": "Normal" },
            { "zone": "FrontRight", "priority": "High"   }
        ]
    })";

    auto m = config::parse_json(json);
    REQUIRE(m.zones.size() == 2);
    REQUIRE(m.zones[0].zone == Zone::FrontLeft);
    REQUIRE(m.zones[1].zone == Zone::FrontRight);
    REQUIRE(m.zones[1].priority == "High");
}

TEST_CASE("config: parse_json unknown zone throws", "[config]") {
    const std::string json = R"({ "zones": [{ "zone": "BadZone" }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: load registers controllers", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "RearLeft"  },
            { "zone": "RearRight" }
        ]
    })";

    // legacy_mock::Registry pre-populates all 5 zones, so it can't show
    // config::load actually registering a new one; use the empty test
    // double instead (see the EmptyRegistry comment above).
    EmptyRegistry preg;
    REQUIRE_FALSE(config::load(json, preg));

    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(preg.lookup(Zone::RearLeft, ctrl));
    REQUIRE_FALSE(preg.lookup(Zone::RearRight, ctrl));
}

TEST_CASE("config: load duplicate zone returns ErrAlreadyExists", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "Central" },
            { "zone": "Central" }
        ]
    })";

    EmptyRegistry preg;
    auto ec = config::load(json, preg);
    REQUIRE(ec == ErrAlreadyExists);
}

TEST_CASE("config: ParseError is a std::runtime_error subclass", "[config][REQ-CFG-006]") {
    // Catchable as std::runtime_error (and thus std::exception) and carries a message.
    bool caught = false;
    try {
        config::parse_json(R"({ "zones": [{ "zone": "Nope" }] })");
    } catch (const std::runtime_error& e) {
        caught = true;
        REQUIRE(std::string(e.what()).find("Nope") != std::string::npos);
    }
    REQUIRE(caught);
    REQUIRE(std::is_base_of<std::runtime_error, config::ParseError>::value);
}
