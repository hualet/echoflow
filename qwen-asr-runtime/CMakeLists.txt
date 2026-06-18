# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(QWEN_ASR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qwen-asr"
    CACHE PATH "qwen-asr source directory")

set(QWEN_ASR_SOURCES
    "${QWEN_ASR_DIR}/qwen_asr.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_generic.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_neon.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_avx.c"
    "${QWEN_ASR_DIR}/qwen_asr_audio.c"
    "${QWEN_ASR_DIR}/qwen_asr_encoder.c"
    "${QWEN_ASR_DIR}/qwen_asr_decoder.c"
    "${QWEN_ASR_DIR}/qwen_asr_tokenizer.c"
    "${QWEN_ASR_DIR}/qwen_asr_safetensors.c")

add_library(qwen_asr STATIC ${QWEN_ASR_SOURCES})
target_include_directories(qwen_asr PUBLIC "${QWEN_ASR_DIR}")

set(ECHOFLOW_TARGET_ARCH "x86-64-v3" CACHE STRING "GCC -march baseline for qwen-asr kernels")
target_compile_options(qwen_asr PRIVATE
    -O3
    -march=${ECHOFLOW_TARGET_ARCH}
    -ffast-math
    -DUSE_BLAS
    -DUSE_OPENBLAS
    -I/usr/include/openblas)

find_package(BLAS REQUIRED)
target_link_libraries(qwen_asr PUBLIC ${BLAS_LIBRARIES} m pthread)
