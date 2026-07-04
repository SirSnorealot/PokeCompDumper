cmake_minimum_required(VERSION 3.16)
project(PokeCompDumper VERSION 1.0 LANGUAGES CXX)

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
    src/util/musicdumper.cpp
    src/util/trainerdumper.cpp
    src/util/overworlddumper.cpp
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
    src/util/musicdumper.h
    src/util/trainerdumper.h
    src/util/overworlddumper.h
    src/mainwindow.h
)

if(WIN32)
    list(APPEND SOURCES src/version.rc)
endif()

qt_add_executable(PokeCompDumper ${SOURCES} ${HEADERS})

target_link_libraries(PokeCompDumper PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    set_target_properties(PokeCompDumper PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_include_directories(PokeCompDumper PRIVATE src src/util)

add_custom_command(TARGET PokeCompDumper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:PokeCompDumper>/config.json"
    COMMENT "Copying config.json to build output"
)
