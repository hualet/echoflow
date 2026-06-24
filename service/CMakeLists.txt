# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(echoflow_service STATIC
    Committer.cpp
    Config.cpp
    CrispAsrEngine.cpp
    CrispLiveVoicePipeline.cpp
    CrispSession.cpp
    Recorder.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)

target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(crispasr SHARED IMPORTED)
set_target_properties(crispasr PROPERTIES
    IMPORTED_LOCATION "$ENV{HOME}/projects/CrispASR/build/src/libcrispasr.so"
    IMPORTED_SONAME "libcrispasr.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "$ENV{HOME}/projects/CrispASR/include"
)
target_link_libraries(echoflow_service PUBLIC crispasr)

add_executable(echoflow-service main.cpp)
target_link_libraries(echoflow-service PRIVATE echoflow_service)
install(TARGETS echoflow-service RUNTIME DESTINATION bin)
