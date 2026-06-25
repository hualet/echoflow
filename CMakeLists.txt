# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(EchoFlow LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(ECHOFLOW_BUILD_TESTS "Build QTest unit tests + spec tests" ON)

# Vendored CrispASR — build as static lib, link C-ABI into echoflow_service.
# Only the qwen3 backend is compiled (other backends stripped at configure time).
set(CRISPASR_WASM OFF CACHE BOOL "" FORCE)
set(CRISPASR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Patch CrispASR for subdirectory use (idempotent):
#   1. fix CMAKE_SOURCE_DIR → CMAKE_CURRENT_SOURCE_DIR for talk-llama path
#   2. strip all backend if(TARGET ...)/endif() blocks except qwen3_asr
#      (handles multi-line target_link_libraries with whole-archive etc.)
execute_process(
    COMMAND grep -q "CMAKE_CURRENT_SOURCE_DIR/../examples/talk-llama"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crispasr/src/CMakeLists.txt"
    RESULT_VARIABLE _already_patched)
if(_already_patched)
    execute_process(
        COMMAND sh -c
        "cd '${CMAKE_CURRENT_SOURCE_DIR}/third_party/crispasr/src' && \
         sed -i 's|\${CMAKE_SOURCE_DIR}/examples/talk-llama|\${CMAKE_CURRENT_SOURCE_DIR}/../examples/talk-llama|g' CMakeLists.txt && \
         awk '
           /^# Adding a new backend/ { in_section=1 }
           in_section && /^if \(TARGET / {
             if (/qwen3_asr/) { print; next }
             depth=1; while (depth>0 && (getline line)>0) {
               if (line ~ /^if /) depth++
               if (line ~ /^endif/) depth--
             }; next
           }
           { print }
         ' CMakeLists.txt > CMakeLists.txt.tmp && mv CMakeLists.txt.tmp CMakeLists.txt")
endif()

add_subdirectory(third_party/crispasr EXCLUDE_FROM_ALL)

add_subdirectory(service)
add_subdirectory(fcitx-addon)
add_subdirectory(ui-host)
if(ECHOFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/systemd/user")
configure_file(
    "${CMAKE_SOURCE_DIR}/systemd/user/echoflow.service.in"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow.service"
    @ONLY)
configure_file(
    "${CMAKE_SOURCE_DIR}/systemd/user/echoflow-ui.service.in"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow-ui.service"
    @ONLY)
install(FILES
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow.service"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow-ui.service"
    DESTINATION "lib/systemd/user")
