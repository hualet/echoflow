# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(echoflow_service STATIC
    Config.cpp
    CrispAsrEngine.cpp
    CrispLiveVoicePipeline.cpp
    CrispStreamAccumulator.cpp
    Committer.cpp
    Recorder.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)

target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(echoflow-service main.cpp)
target_link_libraries(echoflow-service PRIVATE echoflow_service)
install(TARGETS echoflow-service RUNTIME DESTINATION bin)
