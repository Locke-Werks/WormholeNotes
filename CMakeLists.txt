cmake_minimum_required(VERSION 3.21)

project(tondo VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 6.5 is the floor: QStyleHints::colorScheme is how the face follows the
# system light or dark setting.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets PrintSupport)
qt_standard_project_setup()

set(TONDO_SOURCES
    src/main.cpp
    src/TondoWindow.cpp
    src/TondoWindow.h
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
    src/TextFile.cpp
    src/TextFile.h
)

if(WIN32)
    # RC wants forward slashes in a path literal, which is what CMake hands out.
    set(TONDO_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/tondo.ico")
    configure_file(res/tondo.rc.in "${CMAKE_CURRENT_BINARY_DIR}/tondo.rc" @ONLY)
    list(APPEND TONDO_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/tondo.rc")
endif()

qt_add_executable(tondo WIN32 MACOSX_BUNDLE ${TONDO_SOURCES})

qt_add_resources(tondo "icons"
    PREFIX "/"
    BASE assets
    FILES assets/tondo.ico
)

target_compile_definitions(tondo PRIVATE
    TONDO_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(tondo PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tondo PRIVATE -Wall -Wextra)
endif()

target_link_libraries(tondo PRIVATE Qt6::Widgets Qt6::PrintSupport)

install(TARGETS tondo RUNTIME DESTINATION . BUNDLE DESTINATION .)
