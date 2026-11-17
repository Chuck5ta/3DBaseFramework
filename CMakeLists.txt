cmake_minimum_required(VERSION 3.22)

project(Game3D VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Put the executable, shaders and assets together in one folder:
#   build/<preset>/bin/<Config>/
# The $<CONFIG> expression stops multi-config generators (Visual Studio) from
# appending their own sub-folder, so the layout is the same everywhere.
set(GAME_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${GAME_OUTPUT_DIR})

option(GAME_WINDOWS_CONSOLE "Show a console window on Windows (log output)" ON)

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

# --- MikkTSpace: tangents for normal maps (render3d/Tangents.cpp) ---------------------
# Worked out the way Blender, glTF and most bakers do, so baked normal maps line up.
# Game3D only, so it's here rather than in Dependencies.cmake (shared with Game2D).
FetchContent_Declare(mikktspace
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG        3e895b49d05ea07e4c2133156cfa94369e19e409
    SOURCE_SUBDIR  _download_only)
FetchContent_MakeAvailable(mikktspace)
add_library(mikktspace STATIC ${mikktspace_SOURCE_DIR}/mikktspace.c)
target_include_directories(mikktspace SYSTEM PUBLIC ${mikktspace_SOURCE_DIR})
if(UNIX)
    target_link_libraries(mikktspace PRIVATE m)
endif()
set_target_properties(mikktspace PROPERTIES FOLDER "third_party")

# --- Game executable -----------------------------------------------------------
# New .cpp/.h files under src/ are picked up automatically on the next build.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(${PROJECT_NAME} ${GAME_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${GAME_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 third_party imgui mikktspace)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8 /MP)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
    set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${GAME_OUTPUT_DIR})
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(WIN32 AND NOT GAME_WINDOWS_CONSOLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(UNIX AND NOT APPLE)
    # Look for shared libraries next to the executable first (handy if you
    # later ship extra .so files with the game, e.g. on Steam).
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# --- Shaders -> <exe dir>/shaders ----------------------------------------------
file(GLOB GAME_SHADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp)
target_shaders(${PROJECT_NAME} OUTPUT_DIR ${GAME_OUTPUT_DIR}/shaders SOURCES ${GAME_SHADERS})

# --- Assets -> <exe dir>/assets (synced on every build) ----------------------------
add_custom_target(${PROJECT_NAME}_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets ${GAME_OUTPUT_DIR}/assets
    COMMENT "Copying assets")
set_target_properties(${PROJECT_NAME}_assets PROPERTIES FOLDER "build")
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_assets)

# --- Install / packaging -----------------------------------------------------------
# `cmake --install build/release --prefix dist` produces a folder you can zip
# or upload to Steam as a depot.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
install(DIRECTORY ${GAME_OUTPUT_DIR}/shaders DESTINATION .)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION .)
