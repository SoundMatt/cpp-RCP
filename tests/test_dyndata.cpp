// fusa:test REQ-DYN-001
// fusa:test REQ-DYN-002
// fusa:test REQ-DYN-003
// fusa:test REQ-DYN-004
// fusa:test REQ-DYN-005
// fusa:test REQ-DYN-006
#include <catch2/catch_test_macros.hpp>

#include "rcp/dyndata.hpp"

#include <thread>
#include <vector>

using namespace rcp;

TEST_CASE("dyndata: schema registry add and lookup", "[dyndata]") {
    dyndata::SchemaRegistry reg;
    dyndata::SchemaEntry e{0x0001, "SeatPosition",
        {{"angle", "float", 0, 4}, {"height", "float", 4, 4}}};
    REQUIRE_FALSE(reg.add(e));
    REQUIRE(reg.size() == 1);

    dyndata::SchemaEntry out;
    REQUIRE(reg.lookup(0x0001, out));
    REQUIRE(out.name == "SeatPosition");
    REQUIRE(out.fields.size() == 2);
}

TEST_CASE("dyndata: duplicate schema returns ErrAlreadyExists", "[dyndata]") {
    dyndata::SchemaRegistry reg;
    REQUIRE_FALSE(reg.add({0x0002, "A", {}}));
    auto ec = reg.add({0x0002, "B", {}});
    REQUIRE(ec == ErrAlreadyExists);
}

TEST_CASE("dyndata: DynamicPayload encode/decode round-trip", "[dyndata]") {
    dyndata::DynamicPayload dp;
    dp.schema_id = 0xDEADBEEF;
    dp.data      = {0x01, 0x02, 0x03, 0x04};

    auto wire = dp.encode();
    REQUIRE(wire.size() == 8); // 4-byte schema_id + 4 bytes data

    auto dp2 = dyndata::DynamicPayload::decode(wire);
    REQUIRE(dp2.schema_id == 0xDEADBEEF);
    REQUIRE(dp2.data == dp.data);
}

TEST_CASE("dyndata: decode too-short buffer returns zero schema_id", "[dyndata]") {
    auto dp = dyndata::DynamicPayload::decode({0x01, 0x02}); // too short
    REQUIRE(dp.schema_id == 0);
    REQUIRE(dp.data.empty());
}

TEST_CASE("dyndata: unknown schema lookup returns false", "[dyndata][REQ-DYN-002]") {
    dyndata::SchemaRegistry reg;
    dyndata::SchemaEntry out;
    REQUIRE_FALSE(reg.lookup(0xFFFF, out));
}

TEST_CASE("dyndata: encode prepends schema_id big-endian", "[dyndata][REQ-DYN-003]") {
    dyndata::DynamicPayload dp;
    dp.schema_id = 0x01020304;
    dp.data      = {0xAA, 0xBB};
    auto wire = dp.encode();
    REQUIRE(wire.size() == 6);
    // Most-significant byte first.
    REQUIRE(wire[0] == 0x01);
    REQUIRE(wire[1] == 0x02);
    REQUIRE(wire[2] == 0x03);
    REQUIRE(wire[3] == 0x04);
    REQUIRE(wire[4] == 0xAA);
    REQUIRE(wire[5] == 0xBB);
}

TEST_CASE("dyndata: SchemaRegistry is thread-safe", "[dyndata][REQ-DYN-006]") {
    dyndata::SchemaRegistry reg;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                dyndata::SchemaId id =
                    static_cast<dyndata::SchemaId>(t * kPerThread + i + 1);
                auto ec = reg.add({id, "s", {}});
                (void)ec;
                dyndata::SchemaEntry out;
                bool found = reg.lookup(id, out);
                (void)found;
            }
        });
    }
    for (auto& th : ts) th.join();
    // Every distinct id was added exactly once, with no lost updates or crashes.
    REQUIRE(reg.size() == kThreads * kPerThread);
}
