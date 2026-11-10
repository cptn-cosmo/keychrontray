cmake_minimum_required(VERSION 3.22)
project(keychrontray VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets DBus Network Test)
find_package(KF6 6.0 REQUIRED COMPONENTS StatusNotifierItem Notifications Config CoreAddons)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

# KDECompilerSettings disables exceptions and implicit casts; we use neither, keep defaults.
remove_definitions(-DQT_NO_CAST_FROM_ASCII)

add_subdirectory(src)

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES data/keychrontray.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES data/io.github.keychrontray.desktop DESTINATION ${KDE_INSTALL_APPDIR})
# udev rule: by default only installed as a template; packages set e.g. -DKEYCHRONTRAY_UDEV_RULES_DIR=/usr/lib/udev/rules.d
set(KEYCHRONTRAY_UDEV_RULES_DIR "" CACHE PATH "Install the udev rule into this directory (empty = template in share/keychrontray only)")
install(FILES data/70-keychrontray.rules DESTINATION ${KDE_INSTALL_DATADIR}/keychrontray)
if(KEYCHRONTRAY_UDEV_RULES_DIR)
    install(FILES data/70-keychrontray.rules DESTINATION ${KEYCHRONTRAY_UDEV_RULES_DIR})
endif()
