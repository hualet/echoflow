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
set(ECHOFLOW_CPU_TARGET "native" CACHE STRING
    "CPU target for CrispASR: native or x86-64-v3")
set_property(CACHE ECHOFLOW_CPU_TARGET PROPERTY STRINGS native x86-64-v3)

if(ECHOFLOW_CPU_TARGET STREQUAL "x86-64-v3")
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "x86-64-v3 requires an amd64 build host")
    endif()
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_SSE42 ON CACHE BOOL "" FORCE)
    set(GGML_AVX ON CACHE BOOL "" FORCE)
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_BMI2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
    set(GGML_AVX_VNNI OFF CACHE BOOL "" FORCE)
    set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
    set(GGML_AVX512_VBMI OFF CACHE BOOL "" FORCE)
    set(GGML_AVX512_VNNI OFF CACHE BOOL "" FORCE)
    set(GGML_AVX512_BF16 OFF CACHE BOOL "" FORCE)
    set(GGML_AMX_TILE OFF CACHE BOOL "" FORCE)
    set(GGML_AMX_INT8 OFF CACHE BOOL "" FORCE)
    set(GGML_AMX_BF16 OFF CACHE BOOL "" FORCE)
elseif(NOT ECHOFLOW_CPU_TARGET STREQUAL "native")
    message(FATAL_ERROR "Unsupported ECHOFLOW_CPU_TARGET=${ECHOFLOW_CPU_TARGET}")
endif()

# Vendored CrispASR — build as static lib, link C-ABI into echoflow_service.
# Only the qwen3 backend is compiled (other backends stripped at configure time).
set(CRISPASR_WASM OFF CACHE BOOL "" FORCE)
set(CRISPASR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CRISPASR_QWEN3_ONLY ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# EchoFlow feeds live PCM (PipeWire) to the ASR engine — it never decodes
# .opus/.ogg files. Disabling opus skips FetchContent for libopus/libogg/
# opusfile and the miniaudio opus backend (~145 .o).
set(CRISPASR_OPUS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/crispasr EXCLUDE_FROM_ALL)

# EchoFlow only consumes the qwen3 session API. Keep its C API dispatcher and
# the compatibility implementation that the dispatcher still references;
# omit file decoding, device I/O, VAD, diarization, alignment, enhancement,
# chat, and other library surfaces. Adjust the target after CrispASR declares
# it instead of rewriting submodule files, so no-op install-user.sh builds do
# not see changed source mtimes.
get_target_property(_crispasr_sources crispasr-lib SOURCES)
list(FILTER _crispasr_sources INCLUDE REGEX "(^|/)(crispasr|crispasr_c_api)\\.cpp$")
set_property(TARGET crispasr-lib PROPERTY SOURCES "${_crispasr_sources}")

get_target_property(_crispasr_links crispasr-lib LINK_LIBRARIES)
list(FILTER _crispasr_links EXCLUDE REGEX "audioseal|snac|crispasr-llama-core")
set_property(TARGET crispasr-lib PROPERTY LINK_LIBRARIES "${_crispasr_links}")

get_target_property(_crispasr_interface_links crispasr-lib INTERFACE_LINK_LIBRARIES)
list(FILTER _crispasr_interface_links EXCLUDE REGEX "audioseal|snac|crispasr-llama-core")
set_property(TARGET crispasr-lib PROPERTY INTERFACE_LINK_LIBRARIES "${_crispasr_interface_links}")

target_compile_definitions(crispasr-lib PRIVATE CRISPASR_QWEN3_ONLY)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(crispasr-lib PRIVATE -ffunction-sections -fdata-sections)
endif()

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
