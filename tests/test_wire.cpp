// fusa:test REQ-UDP-001
// fusa:test REQ-UDP-002
// fusa:test REQ-UDP-003
// fusa:test REQ-UDP-004
// fusa:test REQ-UDP-005
// fusa:test REQ-UDP-006
// fusa:test REQ-UDP-007
// fusa:test REQ-UDP-008
// fusa:test REQ-UDP-009
// fusa:test REQ-UDP-010
// fusa:test REQ-UDP-011
// fusa:test REQ-UDP-012

#include <catch2/catch_test_macros.hpp>
#include <rcp/wire.hpp>

using namespace rcp;
using namespace rcp::wire;

// ── Header constants ──────────────────────────────────────────────────────────

TEST_CASE("Wire header length and magic are correct", "[wire][REQ-UDP-001]") {
    REQUIRE(HeaderLen == 16);
    REQUIRE(MagicByte0 == 'R');
    REQUIRE(MagicByte1 == 'C');
}

TEST_CASE("Wire version is 1", "[wire][REQ-UDP-002]") {
    REQUIRE(ProtoVer == 1);
}

TEST_CASE("MaxPayload is non-zero", "[wire][REQ-UDP-003]") {
    REQUIRE(MaxPayload > 0);
}

// ── Command round-trip ────────────────────────────────────────────────────────

TEST_CASE("encode_command / decode_command round-trip with no payload", "[wire][REQ-UDP-004]") {
    Command cmd;
    cmd.id       = 0xDEADBEEF;
    cmd.zone     = Zone::FrontLeft;
    cmd.type     = CommandType::Set;
    cmd.priority = Priority::High;

    auto frame = encode_command(cmd);
    REQUIRE(frame.size() >= HeaderLen);

    Command out;
    auto ec = decode_command(frame.data(), frame.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.id       == cmd.id);
    REQUIRE(out.zone     == cmd.zone);
    REQUIRE(out.type     == cmd.type);
    REQUIRE(out.priority == cmd.priority);
    REQUIRE(out.payload.empty());
}

TEST_CASE("encode_command / decode_command round-trip with payload", "[wire][REQ-UDP-005]") {
    Command cmd;
    cmd.id      = 42;
    cmd.zone    = Zone::RearRight;
    cmd.type    = CommandType::Get;
    cmd.payload = {0x01, 0x02, 0x03, 0x04};

    auto frame = encode_command(cmd);
    Command out;
    REQUIRE_FALSE(decode_command(frame.data(), frame.size(), out));
    REQUIRE(out.payload == cmd.payload);
}

TEST_CASE("decode_command rejects truncated frames", "[wire][REQ-UDP-006]") {
    Command cmd;
    cmd.id   = 1;
    cmd.zone = Zone::Central;
    auto frame = encode_command(cmd);

    Command out;
    REQUIRE(decode_command(frame.data(), HeaderLen - 1, out)); // truncated
}

// ── Response round-trip ───────────────────────────────────────────────────────

TEST_CASE("encode_response / decode_response round-trip", "[wire][REQ-UDP-007]") {
    Response resp;
    resp.command_id = 0x1234;
    resp.zone       = Zone::FrontRight;
    resp.status     = ResponseStatus::OK;
    resp.payload    = {0xFF, 0xFE};

    auto frame = encode_response(resp);
    Response out;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), out));
    REQUIRE(out.command_id == resp.command_id);
    REQUIRE(out.zone       == resp.zone);
    REQUIRE(out.status     == resp.status);
    REQUIRE(out.payload    == resp.payload);
}

// ── Status round-trip ─────────────────────────────────────────────────────────

TEST_CASE("encode_status / decode_status round-trip", "[wire][REQ-UDP-008]") {
    Status st;
    st.zone    = Zone::RearLeft;
    st.seq     = 99;
    st.healthy = true;
    st.payload = {0xAB};

    auto frame = encode_status(st);
    Status out;
    REQUIRE_FALSE(decode_status(frame.data(), frame.size(), out));
    REQUIRE(out.zone    == st.zone);
    REQUIRE(out.seq     == st.seq);
    REQUIRE(out.healthy == st.healthy);
    REQUIRE(out.payload == st.payload);
}

// ── Frame magic / version ─────────────────────────────────────────────────────

TEST_CASE("Frames begin with 'RC' magic and correct version", "[wire][REQ-UDP-009]") {
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    auto frame = encode_command(cmd);
    REQUIRE(frame[0] == 'R');
    REQUIRE(frame[1] == 'C');
    REQUIRE(frame[2] == ProtoVer);
}

TEST_CASE("decode_command rejects wrong magic", "[wire][REQ-UDP-010]") {
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    auto frame = encode_command(cmd);
    frame[0] = 'X'; // corrupt magic
    Command out;
    REQUIRE(decode_command(frame.data(), frame.size(), out));
}

TEST_CASE("decode_command rejects wrong version", "[wire][REQ-UDP-011]") {
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    auto frame = encode_command(cmd);
    frame[2] = ProtoVer + 1; // bump version
    Command out;
    REQUIRE(decode_command(frame.data(), frame.size(), out));
}

// ── Control frame ─────────────────────────────────────────────────────────────

TEST_CASE("encode_control produces a valid header-only frame", "[wire][REQ-UDP-012]") {
    auto frame = encode_control(TypeSubscribe, Zone::Central);
    REQUIRE(frame.size() == HeaderLen);
    REQUIRE(frame[0] == 'R');
    REQUIRE(frame[3] == TypeSubscribe);
}
