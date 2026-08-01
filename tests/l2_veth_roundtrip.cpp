// fusa:test REQ-L2-008

// Real-socket round-trip harness for rcp/l2.hpp's rcp::l2::Server/Client —
// NOT a Catch2 suite and NOT part of the normal `ctest` run (see
// tests/CMakeLists.txt's own comment on why: this needs a real
// AF_PACKET/SOCK_RAW socket, CAP_NET_RAW or root, and a real Linux network
// interface pair). Only the dedicated "l2-veth" CI job
// (.github/workflows/ci.yml) actually runs this binary, against a real
// `veth` pair it creates with `sudo ip link add veth0 type veth peer name
// veth1`.
//
// Usage: l2_veth_roundtrip <server_ifname> <client_ifname> <server_mac>
//   server_ifname — interface rcp::l2::Server binds (e.g. "veth1")
//   client_ifname — interface rcp::l2::Client binds (e.g. "veth0")
//   server_mac    — server_ifname's own MAC address, "aa:bb:cc:dd:ee:ff",
//                   read by the calling CI script (e.g. from
//                   /sys/class/net/veth1/address) and passed in as the
//                   Client's caller-supplied destination MAC — this
//                   program does not derive it itself, matching
//                   rcp/l2.hpp::Client's own documented division of
//                   responsibility (the caller decides the destination
//                   MAC; this module never derives one on its own).
//
// Exit code 0 on a verified byte-for-byte round trip, non-zero (with a
// diagnostic on stderr) otherwise.

#include <rcp/l2.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

using namespace rcp;
using namespace rcp::l2;

// The helpers below are only ever called from the RCP_L2_LINUX branch of
// main() (rcp::l2::Server/Client have nothing real to round-trip on any
// other platform — see rcp/l2.hpp's own header comment), so they are
// defined only in that branch too. Otherwise an unused-function warning on
// every other platform's build (this program is part of the default build
// target, per tests/CMakeLists.txt's own comment on why) would turn into a
// hard error under this repository's MSVC /WX setting.
#if defined(RCP_L2_LINUX)

namespace {

bool parse_mac(const std::string& s, MacAddress& out) {
    unsigned bytes[6];
    if (std::sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
                     &bytes[0], &bytes[1], &bytes[2],
                     &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(bytes[i]);
    return true;
}

std::string mac_to_string(const MacAddress& mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

avtp::StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    avtp::StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

} // namespace

#endif // RCP_L2_LINUX

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "l2_veth_roundtrip: FAIL: %s (%s:%d)\n",    \
                          msg, __FILE__, __LINE__);                          \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(int argc, char** argv) {
#if !defined(RCP_L2_LINUX)
    std::fprintf(stderr,
                  "l2_veth_roundtrip: this program requires the Linux "
                  "rcp::l2::Server/Client implementation (RCP_L2_LINUX); "
                  "rcp/l2.hpp's own header comment documents every other "
                  "platform as a function_not_supported stub with nothing "
                  "real to round-trip.\n");
    return 1;
#else
    if (argc != 4) {
        std::fprintf(stderr,
                      "usage: %s <server_ifname> <client_ifname> <server_mac>\n",
                      argv[0]);
        return 2;
    }
    const std::string server_ifname = argv[1];
    const std::string client_ifname = argv[2];

    MacAddress server_mac{};
    CHECK(parse_mac(argv[3], server_mac), "could not parse <server_mac> as aa:bb:cc:dd:ee:ff");

    std::printf("l2_veth_roundtrip: server on %s, client on %s, dest MAC %s\n",
                server_ifname.c_str(), client_ifname.c_str(), mac_to_string(server_mac).c_str());

    Server server(make_stream_id(0x02, 1), server_ifname.c_str());
    CHECK(server.ok(), "Server failed to open/bind its AF_PACKET socket "
                        "(needs CAP_NET_RAW/root, and server_ifname must exist)");
    std::printf("l2_veth_roundtrip: server local MAC %s\n",
                mac_to_string(server.local_mac()).c_str());
    CHECK(server.local_mac() == server_mac,
          "server's own auto-detected MAC does not match the <server_mac> the "
          "caller supplied — interface mismatch?");

    const std::vector<uint8_t> req_payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};

    server.set_handler([&](size_t, const acf::AcfMessageInfo& req,
                            const std::vector<uint8_t>& payload,
                            acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload) {
        out_resp_payload = payload; // echo back byte-for-byte
        out_resp         = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return std::error_code{};
    });

    Client client(make_stream_id(0x03, 1), client_ifname.c_str(), server_mac);
    CHECK(client.ok(), "Client failed to open/bind its AF_PACKET socket "
                        "(needs CAP_NET_RAW/root, and client_ifname must exist)");
    std::printf("l2_veth_roundtrip: client local MAC %s, dest MAC %s\n",
                mac_to_string(client.local_mac()).c_str(),
                mac_to_string(client.dest_mac()).c_str());

    auto req = acf::make_standard_request(/*bus_id=*/7, /*transaction_num=*/42,
                                           /*write=*/true, /*read_size=*/0);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::seconds(5));
    auto ec  = client.request(ctx, req, req_payload, resp, resp_payload);

    CHECK(!ec, "client.request returned an error — no response arrived over the veth pair");
    CHECK(resp.byte_bus_id == 7, "response byte_bus_id does not match the request");
    CHECK(resp.transaction_num == 42, "response transaction_num does not match the request");
    CHECK(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse,
          "response kind is not ReadResponse");
    CHECK(resp_payload == req_payload,
          "response payload does not match the request payload byte-for-byte — "
          "the real Ethernet frame did not round-trip correctly");

    std::printf("l2_veth_roundtrip: OK — %zu-byte payload round-tripped byte-for-byte "
                "over a real veth pair (%s -> %s)\n",
                req_payload.size(), client_ifname.c_str(), server_ifname.c_str());

    server.close();
    client.close();
    return 0;
#endif
}
