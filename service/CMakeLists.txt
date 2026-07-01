# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(echoflow_service STATIC
    AudioSegmenter.cpp
    Committer.cpp
    Config.cpp
    CrispAsrEngine.cpp
    CrispLiveVoicePipeline.cpp
    CrispSession.cpp
    Recorder.cpp
    SegmentAsrWorker.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)

target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(echoflow_service PUBLIC crispasr-lib)

add_executable(echoflow-service main.cpp)
target_link_libraries(echoflow-service PRIVATE echoflow_service)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(echoflow-service PRIVATE "LINKER:--gc-sections")
endif()
install(TARGETS echoflow-service RUNTIME DESTINATION bin)
