// fusa:req REQ-CAPI-001
// fusa:req REQ-CAPI-002
// fusa:req REQ-CAPI-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/capi_impl.hpp"

// ── Helper: static buffers for registry + controller ─────────────────────────

static uint8_t reg_buf[512];
static uint8_t ctrl_buf[64];

TEST_CASE("capi: registry init succeeds with adequate buffer", "[capi]") {
    rcp_registry_h reg = nullptr;
    rcp_err_t err = rcp_registry_init(reg_buf, sizeof(reg_buf), &reg);
    REQUIRE(err == RCP_OK);
    REQUIRE(reg != nullptr);
    rcp_registry_close(reg);
}

TEST_CASE("capi: registry init fails with small buffer", "[capi]") {
    uint8_t small[4];
    rcp_registry_h reg = nullptr;
    rcp_err_t err = rcp_registry_init(small, sizeof(small), &reg);
    REQUIRE(err == RCP_ERR_NOMEM);
}

TEST_CASE("capi: ctrl init succeeds", "[capi]") {
    rcp_ctrl_h ctrl = nullptr;
    rcp_err_t err = rcp_ctrl_init(RCP_ZONE_FRONT_LEFT, ctrl_buf, sizeof(ctrl_buf), &ctrl);
    REQUIRE(err == RCP_OK);
    REQUIRE(ctrl != nullptr);
}

TEST_CASE("capi: ctrl init fails with small buffer", "[capi]") {
    uint8_t tiny[1];
    rcp_ctrl_h ctrl = nullptr;
    rcp_err_t err = rcp_ctrl_init(RCP_ZONE_FRONT_LEFT, tiny, sizeof(tiny), &ctrl);
    REQUIRE(err == RCP_ERR_NOMEM);
}

TEST_CASE("capi: registry_add and send", "[capi]") {
    rcp_registry_h reg  = nullptr;
    rcp_ctrl_h     ctrl = nullptr;

    static uint8_t reg_buf2[512];
    static uint8_t ctrl_buf2[64];

    REQUIRE(rcp_registry_init(reg_buf2, sizeof(reg_buf2), &reg) == RCP_OK);
    REQUIRE(rcp_ctrl_init(RCP_ZONE_FRONT_LEFT, ctrl_buf2, sizeof(ctrl_buf2), &ctrl) == RCP_OK);
    REQUIRE(rcp_registry_add(reg, ctrl) == RCP_OK);

    rcp_command_t cmd{};
    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.cmd_type = 1; // Set
    rcp_response_t resp{};

    auto err = rcp_send(ctrl, &cmd, &resp, 100);
    REQUIRE(err == RCP_OK);
    REQUIRE(resp.status == 0); // OK

    rcp_registry_close(reg);
}

TEST_CASE("capi: rcp_send with null ctrl returns invalid", "[capi]") {
    rcp_command_t cmd{};
    rcp_response_t resp{};
    REQUIRE(rcp_send(nullptr, &cmd, &resp, 100) == RCP_ERR_INVALID);
}

TEST_CASE("capi: sizeof queries return nonzero", "[capi]") {
    REQUIRE(rcp_registry_sizeof() > 0);
    REQUIRE(rcp_ctrl_sizeof() > 0);
}
