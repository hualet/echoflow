# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Thin wrapper that builds the llama.cpp submodule and installs its shared
# runtime libraries (libllama*.so*, libggml*.so*) into the Qwen3-ASR-GGUF
# inference/bin/ directory where echoflow.asr_runner loads them from.
#
# Invocation mirrors fcitx-addon/ and ui-host/:
#   cmake -S llama-runtime -B build/llama-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build/llama-runtime
#   cmake --install build/llama-runtime

cmake_minimum_required(VERSION 3.16)
project(echoflow-llama-runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# llama.cpp's own CMakeLists defines install(TARGETS ...) for ggml/llama which
# default to /usr/local and fail (Permission denied) or pollute system paths if
# the caller doesn't pass -DCMAKE_INSTALL_PREFIX. Our real install target is the
# install(CODE) block at the bottom, which copies runtime libs into
# QWEN_ASR_PROJECT_DIR regardless of the prefix. Redirect the submodule's install
# debris to a build-local discard dir so `cmake --install` succeeds without sudo
# and without polluting system paths. An explicit -DCMAKE_INSTALL_PREFIX=... is
# still honored.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/discard-install"
        CACHE PATH "" FORCE)
endif()

# --- Configuration knobs ---------------------------------------------------
set(ECHOFLOW_LLM_BACKEND "AUTO" CACHE STRING
    "llama.cpp backend: AUTO (Vulkan if SDK present, else CPU) | Vulkan | CPU")
set_property(CACHE ECHOFLOW_LLM_BACKEND PROPERTY STRINGS AUTO Vulkan CPU)

set(QWEN_ASR_PROJECT_DIR "$ENV{HOME}/AI/Model/Qwen3-ASR-GGUF" CACHE PATH
    "Qwen3-ASR-GGUF project dir; runtime libs install into qwen_asr_gguf/inference/bin")

# --- Resolve backend from ECHOFLOW_LLM_BACKEND -----------------------------
if(ECHOFLOW_LLM_BACKEND STREQUAL "AUTO")
    # Request the glslc component because llama.cpp's ggml-vulkan backend
    # requires it at configure time; bare find_package(Vulkan) would report
    # success on loader+headers alone and then fail deep inside the submodule.
    find_package(Vulkan QUIET COMPONENTS glslc)
    if(Vulkan_FOUND)
        message(STATUS "EchoFlow: Vulkan SDK detected — enabling GGML Vulkan backend")
        set(_ECHOFLOW_LLM_VULKAN ON)
    else()
        message(STATUS "EchoFlow: Vulkan SDK not found — building CPU-only GGML")
        set(_ECHOFLOW_LLM_VULKAN OFF)
    endif()
elseif(ECHOFLOW_LLM_BACKEND STREQUAL "Vulkan")
    find_package(Vulkan REQUIRED COMPONENTS glslc)
    message(STATUS "EchoFlow: Vulkan backend forced via ECHOFLOW_LLM_BACKEND")
    set(_ECHOFLOW_LLM_VULKAN ON)
elseif(ECHOFLOW_LLM_BACKEND STREQUAL "CPU")
    message(STATUS "EchoFlow: CPU-only backend forced via ECHOFLOW_LLM_BACKEND")
    set(_ECHOFLOW_LLM_VULKAN OFF)
else()
    message(FATAL_ERROR
        "ECHOFLOW_LLM_BACKEND must be AUTO, Vulkan, or CPU (got: '${ECHOFLOW_LLM_BACKEND}')")
endif()

# --- Flags forwarded into the llama.cpp subdirectory -----------------------
# Force cache vars so ECHOFLOW_LLM_BACKEND stays the single source of truth
# for backend selection (users should not pass -DGGML_VULKAN directly).
set(BUILD_SHARED_LIBS    ON  CACHE BOOL "" FORCE)
set(GGML_VULKAN          ${_ECHOFLOW_LLM_VULKAN} CACHE BOOL "" FORCE)
set(GGML_NATIVE          ON  CACHE BOOL "" FORCE)
set(GGML_CUDA            OFF CACHE BOOL "" FORCE)
# When add_subdirectory'd, LLAMA_STANDALONE is already OFF and these default
# OFF; set them explicitly for clarity and to survive upstream renames.
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/llama.cpp
    llama.cpp-build)

# --- Install: copy runtime .so into qwen_asr_gguf/inference/bin ------------
# llama.cpp's own CMakeLists sets CMAKE_LIBRARY_OUTPUT_DIRECTORY to
# ${CMAKE_BINARY_DIR}/bin, and our wrapper is the top-level project, so the
# built .so files appear at ${CMAKE_CURRENT_BINARY_DIR}/bin/.
#
# cp -a preserves the libllama.so -> libllama.so.0 -> libllama.so.0.0.1
# symlink chain that the dynamic loader expects. CMake's file(COPY) and
# copy_directory dereference symlinks and would flatten the chain, breaking
# runtime loading.
install(CODE "
    file(MAKE_DIRECTORY \"${QWEN_ASR_PROJECT_DIR}/qwen_asr_gguf/inference/bin\")
    file(GLOB LLAMA_RUNTIME_LIBS
        \"${CMAKE_CURRENT_BINARY_DIR}/bin/libllama*.so*\"
        \"${CMAKE_CURRENT_BINARY_DIR}/bin/libggml*.so*\")
    if(NOT LLAMA_RUNTIME_LIBS)
        message(FATAL_ERROR
            \"No llama.cpp shared libraries found under ${CMAKE_CURRENT_BINARY_DIR}/bin — \
did the build produce libllama*.so* / libggml*.so*?\")
    endif()
    execute_process(
        COMMAND cp -a \${LLAMA_RUNTIME_LIBS} \"${QWEN_ASR_PROJECT_DIR}/qwen_asr_gguf/inference/bin/\"
        RESULT_VARIABLE _cp_rc)
    if(_cp_rc)
        message(FATAL_ERROR \"cp -a failed with exit code \${_cp_rc}\")
    endif()
    message(STATUS \"Installed llama.cpp runtime libraries to ${QWEN_ASR_PROJECT_DIR}/qwen_asr_gguf/inference/bin\")
")
