# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(echoflow-ui-host LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets Network)
find_package(Dtk6 COMPONENTS Widget Core Gui REQUIRED)
include(GNUInstallDirs)

add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    ModelDownloader.cpp
    ModelDownloadCoordinator.cpp
    ModelRowWidget.cpp
    UiActivationServer.cpp
    UiActivationHost.cpp
    OnboardingState.cpp
    SetupCommandRunner.cpp
    ModelSetupAdapter.cpp
    OnboardingSetupController.cpp
    OnboardingIllustration.cpp
    OnboardingDialog.cpp
    icons.qrc
    onboarding.qrc
    settings.qrc
    qml.qrc
)

target_include_directories(echoflow-ui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../service
)

target_link_libraries(echoflow-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
        Qt6::Network
        Dtk6::Widget
        Dtk6::Core
        Dtk6::Gui
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/echoflow.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/echoflow.desktop"
    @ONLY)

install(TARGETS echoflow-ui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/echoflow.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/icons/echoflow.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
