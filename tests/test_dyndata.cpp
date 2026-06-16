// fusa:req REQ-DYN-001
// fusa:req REQ-DYN-002
// fusa:req REQ-DYN-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/dyndata.hpp"

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

TEST_CASE("dyndata: unknown schema lookup returns false", "[dyndata]") {
    dyndata::SchemaRegistry reg;
    dyndata::SchemaEntry out;
    REQUIRE_FALSE(reg.lookup(0xFFFF, out));
}
