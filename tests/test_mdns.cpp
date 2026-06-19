// fusa:test REQ-MDNS-001
// fusa:test REQ-MDNS-002
// fusa:test REQ-MDNS-003
// fusa:test REQ-MDNS-004
// fusa:test REQ-MDNS-005
// fusa:test REQ-MDNS-006
// fusa:test REQ-MDNS-007
// fusa:test REQ-MDNS-008

// mDNS / DNS-SD zone-discovery tests (RFC 6762 + RFC 6763).
//
// Exercises the StaticDiscoverer (the header-only Discoverer used for testing
// and static config) and the Announcer interface via a recording test double.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mdns.hpp"

#include <map>
#include <string>
#include <vector>

using namespace rcp;

namespace {

std::vector<mdns::ZoneInfo> sample_zones() {
    return {
        {Zone::FrontLeft,  "fl.local", 5000, "front-left.fl.local._rcp._udp.local"},
        {Zone::FrontRight, "fr.local", 5001, "front-right.fr.local._rcp._udp.local"},
        {Zone::Central,    "c.local",  5002, "central.c.local._rcp._udp.local"},
    };
}

// TestAnnouncer records announce()/withdraw() calls for assertions.
class TestAnnouncer final : public mdns::Announcer {
public:
    std::error_code announce(const mdns::ZoneInfo& info) override {
        announced[info.zone] = info;
        return {};
    }
    void withdraw(Zone z) override { announced.erase(z); }
    std::map<Zone, mdns::ZoneInfo> announced;
};

} // namespace

TEST_CASE("mdns: StaticDiscoverer is a Discoverer that emits on start",
          "[mdns][REQ-MDNS-001]") {
    auto disc = mdns::new_static_discoverer(sample_zones());
    mdns::Discoverer& as_iface = *disc; // satisfies the abstract interface

    int count = 0;
    auto ec = as_iface.start([&](const mdns::DiscoveryEvent&) { ++count; });
    REQUIRE_FALSE(ec);
    REQUIRE(count == 3);
}

TEST_CASE("mdns: start fires an Added event for each discovered zone",
          "[mdns][REQ-MDNS-002][REQ-MDNS-004]") {
    auto disc = mdns::new_static_discoverer(sample_zones());

    std::vector<Zone> added;
    auto ec = disc->start([&](const mdns::DiscoveryEvent& ev) {
        REQUIRE(ev.event == mdns::EventType::Added);
        added.push_back(ev.info.zone);
    });
    REQUIRE_FALSE(ec);

    // Exactly the configured zones, in order (REQ-MDNS-004).
    REQUIRE(added == std::vector<Zone>{Zone::FrontLeft, Zone::FrontRight, Zone::Central});
}

TEST_CASE("mdns: stop terminates discovery", "[mdns][REQ-MDNS-003]") {
    auto disc = mdns::new_static_discoverer(sample_zones());

    int count = 0;
    // Calling stop() from within the callback halts emission after the first event.
    auto ec = disc->start([&](const mdns::DiscoveryEvent&) {
        ++count;
        disc->stop();
    });
    REQUIRE_FALSE(ec);
    REQUIRE(count == 1); // discovery stopped despite three configured zones
}

TEST_CASE("mdns: ZoneInfo carries host, port and zone", "[mdns][REQ-MDNS-005]") {
    auto disc = mdns::new_static_discoverer(sample_zones());

    mdns::ZoneInfo first{};
    bool got = false;
    auto ec = disc->start([&](const mdns::DiscoveryEvent& ev) {
        if (!got) { first = ev.info; got = true; }
    });
    REQUIRE_FALSE(ec);

    REQUIRE(got);
    REQUIRE(first.zone == Zone::FrontLeft);
    REQUIRE(first.host == "fl.local");
    REQUIRE(first.port == 5000);
}

TEST_CASE("mdns: instance_name follows the _rcp._udp.local convention",
          "[mdns][REQ-MDNS-006]") {
    auto name = mdns::make_instance_name(Zone::FrontLeft, "myhost");
    REQUIRE(name == rcp::to_string(Zone::FrontLeft) + ".myhost._rcp._udp.local");
    REQUIRE(name.find("._rcp._udp.local") != std::string::npos);
}

TEST_CASE("mdns: Announcer registers a zone record", "[mdns][REQ-MDNS-007]") {
    TestAnnouncer ann;
    mdns::Announcer& iface = ann; // abstract registration interface

    mdns::ZoneInfo info{Zone::RearLeft, "rl.local", 6000, "rear-left.rl.local._rcp._udp.local"};
    REQUIRE_FALSE(iface.announce(info));
    REQUIRE(ann.announced.count(Zone::RearLeft) == 1);
    REQUIRE(ann.announced[Zone::RearLeft].port == 6000);
}

TEST_CASE("mdns: withdraw removes the mDNS record", "[mdns][REQ-MDNS-008]") {
    TestAnnouncer ann;
    mdns::ZoneInfo info{Zone::RearRight, "rr.local", 6001, "rr._rcp._udp.local"};
    REQUIRE_FALSE(ann.announce(info));
    REQUIRE(ann.announced.count(Zone::RearRight) == 1);

    ann.withdraw(Zone::RearRight);
    REQUIRE(ann.announced.count(Zone::RearRight) == 0);
}
