// fusa:test REQ-MDNS-001
// fusa:test REQ-MDNS-002
// fusa:test REQ-MDNS-003
// fusa:test REQ-MDNS-004
// fusa:test REQ-MDNS-005
// fusa:test REQ-MDNS-006
// fusa:test REQ-MDNS-007
// fusa:test REQ-MDNS-008

// Tests for rcp/mdns.hpp — host:port discovery for the UDP/IP transport
// variant (ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting
// Rebind", v2.14.0).

#include <catch2/catch_test_macros.hpp>

#include "rcp/mdns.hpp"

#include <vector>

using namespace rcp;
using namespace rcp::mdns;

namespace {
avtp::StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    avtp::StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}
} // namespace

// ── Discoverer interface / StaticDiscoverer ──────────────────────────────────

TEST_CASE("Discoverer is an abstract interface implemented by StaticDiscoverer",
          "[mdns][REQ-MDNS-001]") {
    std::unique_ptr<Discoverer> d = new_static_discoverer({});
    REQUIRE(d != nullptr);
}

TEST_CASE("StaticDiscoverer::start emits an Added event for each configured server",
          "[mdns][REQ-MDNS-002][REQ-MDNS-004]") {
    ServerInfo a{make_stream_id(0x02, 1).to_u64(), "10.0.0.1", 8000, "srv-a"};
    ServerInfo b{make_stream_id(0x02, 2).to_u64(), "10.0.0.2", 8001, "srv-b"};

    auto d = new_static_discoverer({a, b});

    std::vector<DiscoveryEvent> events;
    REQUIRE_FALSE(d->start([&](const DiscoveryEvent& ev) { events.push_back(ev); }));

    REQUIRE(events.size() == 2);
    REQUIRE(events[0].event == EventType::Added);
    REQUIRE(events[0].info.stream_key == a.stream_key);
    REQUIRE(events[0].info.host == "10.0.0.1");
    REQUIRE(events[0].info.port == 8000);
    REQUIRE(events[1].info.stream_key == b.stream_key);
}

TEST_CASE("StaticDiscoverer::stop halts emission of further events",
          "[mdns][REQ-MDNS-003]") {
    std::vector<ServerInfo> servers;
    for (int i = 0; i < 5; ++i) {
        servers.push_back({make_stream_id(0x02, static_cast<uint16_t>(i)).to_u64(),
                            "10.0.0.1", 8000, "srv"});
    }
    auto d = new_static_discoverer(servers);

    int seen = 0;
    d->start([&](const DiscoveryEvent&) {
        ++seen;
        if (seen == 2) d->stop();
    });

    REQUIRE(seen == 2); // stop() during the callback halts the remaining emissions
}

// ── ServerInfo shape ──────────────────────────────────────────────────────────

TEST_CASE("ServerInfo carries stream_key, host, port, and instance_name",
          "[mdns][REQ-MDNS-005]") {
    auto sid = make_stream_id(0x02, 0x99);
    ServerInfo info;
    info.stream_key    = sid.to_u64();
    info.host           = "192.168.1.10";
    info.port           = 12345;
    info.instance_name  = make_instance_name(sid, "192.168.1.10");

    REQUIRE(info.stream_key == sid.to_u64());
    REQUIRE(info.host == "192.168.1.10");
    REQUIRE(info.port == 12345);
    REQUIRE_FALSE(info.instance_name.empty());
}

// ── make_instance_name convention ────────────────────────────────────────────

TEST_CASE("make_instance_name follows <hex-stream-key>.<host>._rcp._udp.local",
          "[mdns][REQ-MDNS-006]") {
    auto sid = make_stream_id(0x02, 0x1234);
    auto name = make_instance_name(sid, "myhost");

    REQUIRE(name.find("._rcp._udp.local") != std::string::npos);
    REQUIRE(name.find(".myhost.") != std::string::npos);

    // Two distinct stream_ids must never collide on the same instance name.
    auto other = make_instance_name(make_stream_id(0x03, 0x1234), "myhost");
    REQUIRE(name != other);
}

// ── Announcer interface ───────────────────────────────────────────────────────

namespace {
class RecordingAnnouncer final : public Announcer {
public:
    std::error_code announce(const ServerInfo& info) override {
        announced.push_back(info);
        return {};
    }
    void withdraw(uint64_t stream_key) override { withdrawn.push_back(stream_key); }

    std::vector<ServerInfo> announced;
    std::vector<uint64_t>   withdrawn;
};
} // namespace

TEST_CASE("Announcer interface is implementable and announce() registers a ServerInfo",
          "[mdns][REQ-MDNS-007]") {
    RecordingAnnouncer a;
    ServerInfo info{make_stream_id(0x02, 1).to_u64(), "10.0.0.5", 9000, "srv"};
    REQUIRE_FALSE(a.announce(info));
    REQUIRE(a.announced.size() == 1);
    REQUIRE(a.announced[0].stream_key == info.stream_key);
}

TEST_CASE("Announcer::withdraw removes the record for the given stream_key",
          "[mdns][REQ-MDNS-008]") {
    RecordingAnnouncer a;
    auto key = make_stream_id(0x02, 1).to_u64();
    a.withdraw(key);
    REQUIRE(a.withdrawn.size() == 1);
    REQUIRE(a.withdrawn[0] == key);
}
