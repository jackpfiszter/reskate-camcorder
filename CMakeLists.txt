cmake_minimum_required(VERSION 3.20)
project(reskate_camcorder CXX)

# The camcorder's model: no game, no Windows. ReSkate builds the same sources into ReSkate.dll
# (integration/reskate/README.md); here they build alone so they can be tested anywhere.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(camcorder STATIC
    src/camcorder/audio.cpp
    src/camcorder/camcorder.cpp
    src/camcorder/commands.cpp
    src/camcorder/effects.cpp
    src/camcorder/json.cpp
    src/camcorder/look.cpp
    src/camcorder/preferences.cpp
    src/camcorder/profiles.cpp
    src/camcorder/recorder.cpp
    src/camcorder/rigs.cpp
    src/camcorder/tape.cpp
    src/camcorder/viewfinder.cpp
    src/camcorder/viewfinder_layout.cpp
    src/camcorder/vx1000.cpp)
target_include_directories(camcorder PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(camcorder PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(camcorder PRIVATE /W4 /WX)
else()
    target_compile_options(camcorder PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(camcorder_tests
        tests/test_main.cpp
        tests/audio_tests.cpp
        tests/camcorder_tests.cpp
        tests/command_tests.cpp
        tests/effect_tests.cpp
        tests/look_tests.cpp
        tests/preference_tests.cpp
        tests/profile_tests.cpp
        tests/recorder_tests.cpp
        tests/rig_tests.cpp
        tests/tape_tests.cpp
        tests/viewfinder_tests.cpp
        tests/vx1000_tests.cpp)
    target_link_libraries(camcorder_tests PRIVATE camcorder)
    # The example mod must always load cleanly: modders copy it.
    target_compile_definitions(camcorder_tests PRIVATE CAMCORDER_EXAMPLE_MODS="${CMAKE_CURRENT_SOURCE_DIR}/examples/mods")
    # Every effect in the library must have its program in the picture shader.
    target_compile_definitions(camcorder_tests PRIVATE CAMCORDER_SHADER="${CMAKE_CURRENT_SOURCE_DIR}/integration/reskate/Extension/Camcorder/camcorder_shader.h")
    add_test(NAME camcorder_tests COMMAND camcorder_tests)
endif()
