cmake_minimum_required(VERSION 3.21)

project(WormholeNotes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 6.5 is the floor: QStyleHints::colorScheme is how the face follows the
# system light or dark setting.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

set(WORMHOLENOTES_SOURCES
    src/main.cpp
    src/Wormhole.cpp
    src/Wormhole.h
    src/PlaceTracker.cpp
    src/PlaceTracker.h
    src/SheetStore.cpp
    src/SheetStore.h
    src/HoleWindow.cpp
    src/HoleWindow.h
    src/NoteWindow.cpp
    src/NoteWindow.h
    src/Arc.cpp
    src/Arc.h
    src/RingTextLayout.cpp
    src/RingTextLayout.h
    src/RoundEdit.cpp
    src/RoundEdit.h
    src/RadialMenu.cpp
    src/RadialMenu.h
    src/RadialPrompt.cpp
    src/RadialPrompt.h
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

target_compile_definitions(WormholeNotes PRIVATE
    WORMHOLENOTES_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(WormholeNotes PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(WormholeNotes PRIVATE -Wall -Wextra)
endif()

# dwmapi for the visible frame and cloaking of the window the hole rides on.
target_link_libraries(WormholeNotes PRIVATE Qt6::Widgets dwmapi)

install(TARGETS WormholeNotes RUNTIME DESTINATION .)
