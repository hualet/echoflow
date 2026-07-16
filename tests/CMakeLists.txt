# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(echoflow-focused-tests LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)

    find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
    enable_testing()

    add_executable(test_onboarding_state
        test_onboarding_state.cpp
        ../ui-host/OnboardingState.cpp)
    target_include_directories(test_onboarding_state PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host)
    target_link_libraries(test_onboarding_state PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME test_onboarding_state COMMAND test_onboarding_state)

    add_executable(test_ui_activation_server
        test_ui_activation_server.cpp
        ../ui-host/UiActivationServer.cpp)
    target_include_directories(test_ui_activation_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host)
    target_link_libraries(test_ui_activation_server PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME test_ui_activation_server COMMAND test_ui_activation_server)

    add_executable(test_onboarding_setup_controller
        test_onboarding_setup_controller.cpp
        ../ui-host/ModelSetupAdapter.cpp
        ../ui-host/ModelDownloadCoordinator.cpp
        ../ui-host/ModelDownloader.cpp
        ../ui-host/OnboardingSetupController.cpp
        ../ui-host/SetupCommandRunner.cpp
        ../ui-host/OnboardingState.cpp)
    target_include_directories(test_onboarding_setup_controller PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host
        ${CMAKE_CURRENT_SOURCE_DIR}/../service)
    target_link_libraries(test_onboarding_setup_controller PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME test_onboarding_setup_controller
        COMMAND test_onboarding_setup_controller)
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Network Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
    test_text_joiner
    test_vad_metrics
    test_vad_backend
    test_single_instance
    test_benchmark_manifest)

foreach(test_target ${ECHOFLOW_TESTS})
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE echoflow_service Qt6::Test pthread)
    echoflow_enable_dead_strip(${test_target})
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_executable(test_onboarding_state
    test_onboarding_state.cpp
    ../ui-host/OnboardingState.cpp)
target_include_directories(test_onboarding_state PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host)
target_link_libraries(test_onboarding_state PRIVATE Qt6::Core Qt6::Test)
echoflow_enable_dead_strip(test_onboarding_state)
add_test(NAME test_onboarding_state COMMAND test_onboarding_state)

add_executable(test_ui_activation_server
    test_ui_activation_server.cpp
    ../ui-host/UiActivationServer.cpp)
target_include_directories(test_ui_activation_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host)
target_link_libraries(test_ui_activation_server PRIVATE
    Qt6::Core Qt6::Network Qt6::Test)
echoflow_enable_dead_strip(test_ui_activation_server)
add_test(NAME test_ui_activation_server COMMAND test_ui_activation_server)

add_executable(test_onboarding_setup_controller
    test_onboarding_setup_controller.cpp
    ../ui-host/ModelSetupAdapter.cpp
    ../ui-host/ModelDownloadCoordinator.cpp
    ../ui-host/ModelDownloader.cpp
    ../ui-host/OnboardingSetupController.cpp
    ../ui-host/SetupCommandRunner.cpp
    ../ui-host/OnboardingState.cpp)
target_include_directories(test_onboarding_setup_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui-host
    ${CMAKE_CURRENT_SOURCE_DIR}/../service)
target_link_libraries(test_onboarding_setup_controller PRIVATE
    Qt6::Core Qt6::Network Qt6::Test)
echoflow_enable_dead_strip(test_onboarding_setup_controller)
add_test(NAME test_onboarding_setup_controller
    COMMAND test_onboarding_setup_controller)

target_include_directories(test_benchmark_manifest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
target_sources(test_benchmark_manifest PRIVATE benchmarks/BenchmarkManifest.cpp)

add_test(NAME spec_tests
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/spec/run_spec.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME performance_gate_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v tests.test_performance_gate)
set_tests_properties(performance_gate_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME release_performance_driver_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v tests.test_release_performance_driver)
set_tests_properties(release_performance_driver_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME release_evidence_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v tests.test_release_evidence)
set_tests_properties(release_evidence_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(voice_latency_benchmark benchmarks/voice_latency_benchmark.cpp)
target_link_libraries(voice_latency_benchmark PRIVATE echoflow_service pthread)
echoflow_enable_dead_strip(voice_latency_benchmark)

add_executable(vad_replay_benchmark benchmarks/vad_replay_benchmark.cpp)
target_sources(vad_replay_benchmark PRIVATE benchmarks/BenchmarkManifest.cpp)
target_include_directories(vad_replay_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
target_link_libraries(vad_replay_benchmark PRIVATE echoflow_service Qt6::Core pthread)
echoflow_enable_dead_strip(vad_replay_benchmark)
