cmake_minimum_required(VERSION 3.16)

project(EchoIR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(echoir
    src/ac.cpp
    src/ac_official.cpp
    src/code_store.cpp
    src/device.cpp
    src/frame.cpp
    src/serial.cpp
)

target_include_directories(echoir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_compile_definitions(echoir PRIVATE ECHOIR_HAS_WIN32_SERIAL=1)
elseif(UNIX)
    target_compile_definitions(echoir PRIVATE ECHOIR_HAS_POSIX_SERIAL=1)
endif()

target_compile_options(echoir PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(echoir-cli src/cli.cpp)
target_link_libraries(echoir-cli PRIVATE echoir)
set_target_properties(echoir-cli PROPERTIES OUTPUT_NAME echoir)

if(MINGW)
    target_link_options(echoir-cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()

enable_testing()

add_executable(echoir_tests tests/test_echoir.cpp)
target_link_libraries(echoir_tests PRIVATE echoir)
target_compile_options(echoir_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
if(MINGW)
    target_link_options(echoir_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME echoir_tests COMMAND echoir_tests)
