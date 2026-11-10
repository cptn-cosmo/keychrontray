add_library(keychrontray_core STATIC
    core/logging.cpp
    core/ledstate.cpp
    core/viaprotocol.cpp
    core/keyboardbackend.cpp
    core/hidkeyboardbackend.cpp
    core/mockkeyboardbackend.cpp
    core/devicemonitor.cpp
    core/batteryprovider.cpp
    core/batteryalerts.cpp
    core/batteryicon.cpp
    core/profile.cpp
    core/profilemanager.cpp
    core/lightingcontroller.cpp
    core/gamerules.cpp
    core/processscanner.cpp
    core/gamedetector.cpp
    core/vdf.cpp
    core/steamlibrary.cpp
    core/steamwebapi.cpp
    core/appsettings.cpp
    ../data/defaults.qrc
)
target_include_directories(keychrontray_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(keychrontray_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::DBus Qt6::Network
    KF6::ConfigCore
    PkgConfig::HIDAPI PkgConfig::UDEV
)

add_library(keychrontray_app STATIC
    app/cli.cpp
    app/traycontroller.cpp
    app/dbusinterface.cpp
    app/settingsdialog.cpp
    app/steampickerdialog.cpp
    app/autostart.cpp
)
target_include_directories(keychrontray_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(keychrontray_app PUBLIC
    keychrontray_core
    Qt6::Widgets
    KF6::StatusNotifierItem KF6::Notifications KF6::CoreAddons
)

add_executable(keychrontray app/main.cpp)
target_link_libraries(keychrontray PRIVATE keychrontray_app)
install(TARGETS keychrontray ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
