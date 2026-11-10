foreach(t via profile matcher batteryalerts lighting steam)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE keychrontray_core Qt6::Test)
    add_test(NAME ${t} COMMAND test_${t})
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endforeach()

# UI smoke test: builds the settings window with the mock backend (offscreen, temp config dir).
# Set KEYCHRONTRAY_SCREENSHOT_DIR to also save a PNG per tab.
add_executable(test_settingsui test_settingsui.cpp)
target_link_libraries(test_settingsui PRIVATE keychrontray_app Qt6::Test)
add_test(NAME settingsui COMMAND test_settingsui)
set_tests_properties(settingsui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
