# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)
qt_standard_project_setup()

set(ECHOFLOW_TESTS
    test_asr_engine
    test_config
    test_voice_session
    test_committer
    test_selftest
    test_model_catalog
    test_single_instance)

foreach(test_target ${ECHOFLOW_TESTS})
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE echoflow_service Qt6::Test pthread)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_test(NAME spec_tests
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/spec/run_spec.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(voice_latency_benchmark benchmarks/voice_latency_benchmark.cpp)
target_link_libraries(voice_latency_benchmark PRIVATE echoflow_service pthread)
