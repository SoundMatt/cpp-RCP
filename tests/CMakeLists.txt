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

add_executable(bench_mock bench_mock.cpp)
target_link_libraries(bench_mock PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_bench COMMAND bench_mock)

add_executable(command_latency_test command_latency_test.cpp)
target_link_libraries(command_latency_test PRIVATE rcp::rcp Catch2::Catch2WithMain)
add_test(NAME rcp_latency COMMAND command_latency_test)
set_tests_properties(rcp_latency PROPERTIES TIMEOUT 60)
