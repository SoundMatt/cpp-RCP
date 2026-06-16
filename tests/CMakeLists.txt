add_executable(test_rcp test_rcp.cpp)
target_link_libraries(test_rcp PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_core COMMAND test_rcp)

add_executable(test_mock test_mock.cpp)
target_link_libraries(test_mock PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_mock COMMAND test_mock)

add_executable(test_wire test_wire.cpp)
target_link_libraries(test_wire PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_wire COMMAND test_wire)

add_executable(test_e2e test_e2e.cpp)
target_link_libraries(test_e2e PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_e2e COMMAND test_e2e)

add_executable(test_prioqueue test_prioqueue.cpp)
target_link_libraries(test_prioqueue PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_prioqueue COMMAND test_prioqueue)

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

add_executable(test_shmem test_shmem.cpp)
target_link_libraries(test_shmem PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_shmem COMMAND test_shmem)

# ── Milestones 19–40 ──────────────────────────────────────────────────────────

add_executable(test_authz test_authz.cpp)
target_link_libraries(test_authz PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_authz COMMAND test_authz)

add_executable(test_firmware test_firmware.cpp)
target_link_libraries(test_firmware PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_firmware COMMAND test_firmware)

add_executable(test_zonegroup test_zonegroup.cpp)
target_link_libraries(test_zonegroup PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_zonegroup COMMAND test_zonegroup)

add_executable(test_proxy test_proxy.cpp)
target_link_libraries(test_proxy PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_proxy COMMAND test_proxy)

add_executable(test_redundancy test_redundancy.cpp)
target_link_libraries(test_redundancy PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_redundancy COMMAND test_redundancy)

add_executable(test_federation test_federation.cpp)
target_link_libraries(test_federation PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_federation COMMAND test_federation)

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
