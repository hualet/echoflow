# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(echoflow_service STATIC
    AudioSegmenter.cpp
    Committer.cpp
    Config.cpp
    CrispAsrEngine.cpp
    CrispLiveVoicePipeline.cpp
    CrispSession.cpp
    LiveDebugRecorder.cpp
    LiveSegmentCoordinator.cpp
    LiveSegmenterFactory.cpp
    LocalLlmPolisher.cpp
    PolishPrompt.cpp
    Recorder.cpp
    SegmentAsrWorker.cpp
    SelfTest.cpp
    SileroLiveSegmenter.cpp
    SileroVadBackend.cpp
    Server.cpp
    SingleInstance.cpp
    TextJoiner.cpp
    UiNotifier.cpp
    VadMetrics.cpp
    VoiceSession.cpp)

target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(echoflow_service PUBLIC crispasr-lib echoflow_crisp_chat)
# Only for llama.h's log hook in LocalLlmPolisher.cpp.
target_link_libraries(echoflow_service PRIVATE crispasr-llama-core)

add_executable(echoflow-service main.cpp)
target_link_libraries(echoflow-service PRIVATE echoflow_service)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(echoflow-service PRIVATE "LINKER:--gc-sections")
endif()
install(TARGETS echoflow-service RUNTIME DESTINATION bin)
