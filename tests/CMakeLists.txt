# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)
qt_standard_project_setup()

function(echoflow_enable_dead_strip target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(${target} PRIVATE "LINKER:--gc-sections")
    endif()
endfunction()

set(ECHOFLOW_TESTS
    test_audio_segmenter
    test_config
    test_crispasr_engine
    test_voice_session
    test_committer
    test_selftest
    test_model_catalog
    test_recorder_command
    test_segment_asr_worker
    test_vad_metrics
    test_vad_backend
    test_single_instance)

foreach(test_target ${ECHOFLOW_TESTS})
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE echoflow_service Qt6::Test pthread)
    echoflow_enable_dead_strip(${test_target})
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_test(NAME spec_tests
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/spec/run_spec.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(voice_latency_benchmark benchmarks/voice_latency_benchmark.cpp)
target_link_libraries(voice_latency_benchmark PRIVATE echoflow_service pthread)
echoflow_enable_dead_strip(voice_latency_benchmark)

add_executable(vad_replay_benchmark benchmarks/vad_replay_benchmark.cpp)
target_link_libraries(vad_replay_benchmark PRIVATE echoflow_service Qt6::Core pthread)
echoflow_enable_dead_strip(vad_replay_benchmark)
