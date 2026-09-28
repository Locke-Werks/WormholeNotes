cmake_minimum_required(VERSION 3.21)

project(WormholeNotes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 6.5 is the floor: QStyleHints::colorScheme is how the face follows the
# system light or dark setting.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

set(WORMHOLENOTES_SOURCES
    src/main.cpp
    src/Wormhole.cpp
    src/Wormhole.h
    src/PlaceTracker.cpp
    src/PlaceTracker.h
    src/BrowserReader.cpp
    src/BrowserReader.h
    src/SheetStore.cpp
    src/SheetStore.h
    src/HoleWindow.cpp
    src/HoleWindow.h
    src/DeskNote.cpp
    src/DeskNote.h
    src/TearTarget.cpp
    src/TearTarget.h
    src/SearchWindow.cpp
    src/SearchWindow.h
    src/Hotkey.cpp
    src/Hotkey.h
    src/NativeHost.cpp
    src/NativeHost.h
    src/NoteWindow.cpp
    src/NoteWindow.h
    src/NoteFace.cpp
    src/NoteFace.h
    src/Theme.cpp
    src/Theme.h
    src/SpiralLayout.cpp
    src/SpiralLayout.h
    src/Round.cpp
    src/Round.h
    src/Updater.cpp
    src/Updater.h
    src/SettingsWindow.cpp
    src/SettingsWindow.h
    src/SignIn.cpp
    src/SignIn.h
)

if(WIN32)
    # RC wants forward slashes in a path literal, which is what CMake hands out.
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/wormholenotes.ico")
    configure_file(res/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    list(APPEND WORMHOLENOTES_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

qt_add_executable(WormholeNotes WIN32 ${WORMHOLENOTES_SOURCES})

qt_add_resources(WormholeNotes "icons"
    PREFIX "/"
    BASE assets
    FILES assets/wormholenotes.ico
)

# The house faces, the same files AutoPM ships, all under the SIL Open Font
# License.
set(HOUSE_FONTS
    assets/fonts/ChakraPetch-SemiBold.ttf
    assets/fonts/ChakraPetch-Medium.ttf
    assets/fonts/InstrumentSerif-Regular.ttf
    assets/fonts/Outfit-Variable.ttf
)
qt_add_resources(WormholeNotes "fonts" PREFIX "/" BASE assets FILES ${HOUSE_FONTS})

target_compile_definitions(WormholeNotes PRIVATE
    WORMHOLENOTES_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(WormholeNotes PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(WormholeNotes PRIVATE -Wall -Wextra)
endif()

# dwmapi for the visible frame and cloaking of the window the hole rides on,
# ole32 and oleaut32 for UI Automation.
# Network for the local socket the extension's host relays through and the
# update check; wintrust and crypt32 to check an update's signature.
target_link_libraries(WormholeNotes PRIVATE Qt6::Widgets Qt6::Network dwmapi ole32 oleaut32 wintrust crypt32)

install(TARGETS WormholeNotes RUNTIME DESTINATION .)
# The browser extension, for loading unpacked until it is in a store.
install(DIRECTORY extension/ DESTINATION extension)
# The OFL asks for its text to travel with the fonts.
install(FILES assets/fonts/OFL-ChakraPetch.txt assets/fonts/OFL-InstrumentSerif.txt assets/fonts/OFL-Outfit.txt DESTINATION licenses)

enable_testing()
qt_add_executable(wormhole-store-test tests/store.cpp src/SheetStore.cpp src/SheetStore.h)
target_link_libraries(wormhole-store-test PRIVATE Qt6::Core)
add_test(NAME store COMMAND wormhole-store-test)
qt_add_executable(wormhole-spiral-test tests/spiral.cpp src/SpiralLayout.cpp src/SpiralLayout.h)
target_link_libraries(wormhole-spiral-test PRIVATE Qt6::Gui)
# Offscreen, so the test runs on CI with no display.
add_test(NAME spiral COMMAND wormhole-spiral-test -platform offscreen)
qt_add_executable(wormhole-updater-test tests/updater.cpp src/Updater.cpp src/Updater.h)
target_link_libraries(wormhole-updater-test PRIVATE Qt6::Gui Qt6::Network wintrust crypt32)
add_test(NAME updater COMMAND wormhole-updater-test -platform offscreen)
# Another app's Qt on PATH would otherwise be loaded ahead of the one built
# against, and a mismatched Qt6Gui fails fast before main.
set_tests_properties(store spiral updater PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# A console probe of place detection on the real desktop. Not installed.
qt_add_executable(wormhole-probe
    tests/probe.cpp
    src/NativeHost.cpp
    src/NativeHost.h
    src/PlaceTracker.cpp
    src/PlaceTracker.h
    src/BrowserReader.cpp
    src/BrowserReader.h
)
if(MSVC)
    target_compile_options(wormhole-probe PRIVATE /W4 /permissive- /utf-8)
endif()
target_link_libraries(wormhole-probe PRIVATE Qt6::Core Qt6::Network dwmapi ole32 oleaut32)

# Renders the note offscreen to PNG, for looking at the bezel. Not installed.
qt_add_executable(wormhole-render
    tests/render.cpp
    src/NoteWindow.cpp
    src/NoteWindow.h
    src/NoteFace.cpp
    src/NoteFace.h
    src/Theme.cpp
    src/Theme.h
    src/SpiralLayout.cpp
    src/SpiralLayout.h
    src/Round.cpp
    src/Round.h
)
qt_add_resources(wormhole-render "fonts" PREFIX "/" BASE assets FILES ${HOUSE_FONTS})
target_link_libraries(wormhole-render PRIVATE Qt6::Widgets)
