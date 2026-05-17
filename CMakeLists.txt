cmake_minimum_required(VERSION 3.16)
project(PokeMapExport VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/globals.cpp
    src/util/hex.cpp
    src/util/romconfig.cpp
    src/util/lz77.cpp
    src/util/poketext.cpp
    src/util/image.cpp
    src/util/map.cpp
    src/util/pokescript.cpp
    src/mainwindow.cpp
    src/mainwindow.ui
)

set(HEADERS
    src/globals.h
    src/util/hex.h
    src/util/romconfig.h
    src/util/lz77.h
    src/util/poketext.h
    src/util/image.h
    src/util/map.h
    src/util/pokescript.h
    src/mainwindow.h
)

if(WIN32)
    list(APPEND SOURCES src/version.rc)
endif()

qt_add_executable(PokeMapExport ${SOURCES} ${HEADERS})

target_link_libraries(PokeMapExport PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    set_target_properties(PokeMapExport PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_include_directories(PokeMapExport PRIVATE src src/util)

add_custom_command(TARGET PokeMapExport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:PokeMapExport>/config.json"
    COMMENT "Copying config.json to build output"
)
