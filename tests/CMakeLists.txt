add_executable(test_rcp test_rcp.cpp)
target_link_libraries(test_rcp PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_core COMMAND test_rcp)

add_executable(test_mock test_mock.cpp)
target_link_libraries(test_mock PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_mock COMMAND test_mock)

add_executable(test_avtp test_avtp.cpp)
target_link_libraries(test_avtp PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_avtp COMMAND test_avtp)

add_executable(test_acf test_acf.cpp)
target_link_libraries(test_acf PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_acf COMMAND test_acf)

add_executable(test_lifecycle test_lifecycle.cpp)
target_link_libraries(test_lifecycle PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_lifecycle COMMAND test_lifecycle)

add_executable(test_regmap test_regmap.cpp)
target_link_libraries(test_regmap PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_regmap COMMAND test_regmap)

add_executable(test_discovery test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_discovery COMMAND test_discovery)

add_executable(test_endpoint test_endpoint.cpp)
target_link_libraries(test_endpoint PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_endpoint COMMAND test_endpoint)

add_executable(test_gpio test_gpio.cpp)
target_link_libraries(test_gpio PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_gpio COMMAND test_gpio)

add_executable(test_spi test_spi.cpp)
target_link_libraries(test_spi PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_spi COMMAND test_spi)

add_executable(test_i2c test_i2c.cpp)
target_link_libraries(test_i2c PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_i2c COMMAND test_i2c)

add_executable(test_uart test_uart.cpp)
target_link_libraries(test_uart PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_uart COMMAND test_uart)

add_executable(test_adc test_adc.cpp)
target_link_libraries(test_adc PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_adc COMMAND test_adc)

add_executable(test_pwm test_pwm.cpp)
target_link_libraries(test_pwm PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_pwm COMMAND test_pwm)

add_executable(test_request test_request.cpp)
target_link_libraries(test_request PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_request COMMAND test_request)

add_executable(test_server test_server.cpp)
target_link_libraries(test_server PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_server COMMAND test_server)

add_executable(test_e2e test_e2e.cpp)
target_link_libraries(test_e2e PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_e2e COMMAND test_e2e)

add_executable(test_lin test_lin.cpp)
target_link_libraries(test_lin PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_lin COMMAND test_lin)

add_executable(test_can test_can.cpp)
target_link_libraries(test_can PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_can COMMAND test_can)

add_executable(test_iseled test_iseled.cpp)
target_link_libraries(test_iseled PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_iseled COMMAND test_iseled)

add_executable(test_mdio test_mdio.cpp)
target_link_libraries(test_mdio PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_mdio COMMAND test_mdio)

add_executable(test_wakeup test_wakeup.cpp)
target_link_libraries(test_wakeup PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_wakeup COMMAND test_wakeup)

add_executable(test_ratelimit test_ratelimit.cpp)
target_link_libraries(test_ratelimit PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_ratelimit COMMAND test_ratelimit)

add_executable(test_watchdog test_watchdog.cpp)
target_link_libraries(test_watchdog PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_watchdog COMMAND test_watchdog)

add_executable(test_deadline test_deadline.cpp)
target_link_libraries(test_deadline PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_deadline COMMAND test_deadline)

add_executable(test_sim test_sim.cpp)
target_link_libraries(test_sim PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_sim COMMAND test_sim)

add_executable(test_faultinject test_faultinject.cpp)
target_link_libraries(test_faultinject PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_faultinject COMMAND test_faultinject)

add_executable(test_loan test_loan.cpp)
target_link_libraries(test_loan PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_loan COMMAND test_loan)

add_executable(test_fragment test_fragment.cpp)
target_link_libraries(test_fragment PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_fragment COMMAND test_fragment)

add_executable(test_respqueue test_respqueue.cpp)
target_link_libraries(test_respqueue PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_respqueue COMMAND test_respqueue)

add_executable(test_shmem test_shmem.cpp)
target_link_libraries(test_shmem PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_shmem COMMAND test_shmem)

add_executable(test_alloc test_alloc.cpp)
target_link_libraries(test_alloc PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_alloc COMMAND test_alloc)

# ── Milestones 19–40 ──────────────────────────────────────────────────────────

add_executable(test_authz test_authz.cpp)
target_link_libraries(test_authz PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_authz COMMAND test_authz)

add_executable(test_redundancy test_redundancy.cpp)
target_link_libraries(test_redundancy PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_redundancy COMMAND test_redundancy)

add_executable(test_observe test_observe.cpp)
target_link_libraries(test_observe PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_observe COMMAND test_observe)

add_executable(test_admin test_admin.cpp)
target_link_libraries(test_admin PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_admin COMMAND test_admin)

add_executable(test_record test_record.cpp)
target_link_libraries(test_record PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_record COMMAND test_record)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_config COMMAND test_config)

add_executable(test_dyndata test_dyndata.cpp)
target_link_libraries(test_dyndata PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_dyndata COMMAND test_dyndata)

add_executable(test_capi test_capi.cpp)
target_link_libraries(test_capi PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_capi COMMAND test_capi)

# ── Benchmark ─────────────────────────────────────────────────────────────────

add_executable(bench_mock bench_mock.cpp)
target_link_libraries(bench_mock PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_bench COMMAND bench_mock)

add_executable(command_latency_test command_latency_test.cpp)
target_link_libraries(command_latency_test PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_latency COMMAND command_latency_test)
set_tests_properties(rcp_latency PROPERTIES TIMEOUT 60)

# ── RELAY conformance ─────────────────────────────────────────────────────────

add_executable(test_relay test_relay.cpp)
target_link_libraries(test_relay PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_relay COMMAND test_relay)

add_executable(test_adapt test_adapt.cpp)
target_link_libraries(test_adapt PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_adapt COMMAND test_adapt)

add_executable(test_cli test_cli.cpp)
target_link_libraries(test_cli PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_cli COMMAND test_cli)

# ── Transport / bridge stubs + remaining modules ─────────────────────────────

foreach(mod
    ddsbr doipbr grpcbridge mqttbr restbridge someipbr udsbr
    tsn mdns powerstate tls udp l2)
  add_executable(test_${mod} test_${mod}.cpp)
  target_link_libraries(test_${mod} PRIVATE rcp::rcp Catch2::Catch2WithMain)
  add_test(NAME rcp_${mod} COMMAND test_${mod})
endforeach()

# ── rcp/l2.hpp real-socket round trip (Linux-only, needs CAP_NET_RAW/root) ───
# l2_veth_roundtrip is a standalone program, not a Catch2 suite, and is
# deliberately NOT registered via add_test: rcp::l2::Server/Client need a
# real AF_PACKET/SOCK_RAW socket (CAP_NET_RAW or root) and a real network
# interface pair, neither of which the normal `ctest` run has or should
# require (see tests/test_l2.cpp's own header comment — that file covers
# the privilege-free, socket-free codec logic this program does not
# duplicate). Built here so it's available to build; only the dedicated
# "l2-veth" CI job (.github/workflows/ci.yml) actually creates a veth pair
# and runs this binary with sudo.
add_executable(l2_veth_roundtrip l2_veth_roundtrip.cpp)
target_link_libraries(l2_veth_roundtrip PRIVATE rcp::rcp)
