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

find_program(NPM_EXECUTABLE npm)
if(NPM_EXECUTABLE)
    set(ECHOIR_WEB_ASSETS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/echoir_web_assets.cpp)
    set(ECHOIR_WEB_ASSETS_HPP ${CMAKE_CURRENT_BINARY_DIR}/generated/echoir_web_assets.hpp)
    add_custom_command(
        OUTPUT
            ${ECHOIR_WEB_ASSETS_CPP}
            ${ECHOIR_WEB_ASSETS_HPP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND ${NPM_EXECUTABLE} install
        COMMAND ${NPM_EXECUTABLE} run build
        COMMAND ${CMAKE_COMMAND}
            -DWEB_DIST=${CMAKE_CURRENT_SOURCE_DIR}/web/dist
            -DOUT_CPP=${ECHOIR_WEB_ASSETS_CPP}
            -DOUT_HPP=${ECHOIR_WEB_ASSETS_HPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWebAssets.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/web
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWebAssets.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
            ${CMAKE_CURRENT_SOURCE_DIR}/web/package-lock.json
            ${CMAKE_CURRENT_SOURCE_DIR}/web/package.json
            ${CMAKE_CURRENT_SOURCE_DIR}/web/src/main.jsx
            ${CMAKE_CURRENT_SOURCE_DIR}/web/src/styles.css
            ${CMAKE_CURRENT_SOURCE_DIR}/web/vite.config.js
        COMMENT "Building and embedding EchoIR web UI"
        VERBATIM
    )
    add_custom_target(echoir_web_assets DEPENDS ${ECHOIR_WEB_ASSETS_CPP} ${ECHOIR_WEB_ASSETS_HPP})
    add_dependencies(echoir-cli echoir_web_assets)
    target_sources(echoir-cli PRIVATE src/web_server.cpp ${ECHOIR_WEB_ASSETS_CPP})
    target_include_directories(echoir-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_compile_definitions(echoir-cli PRIVATE ECHOIR_HAS_EMBEDDED_WEB=1)
else()
    message(WARNING "npm was not found; embedded web server will not be built into echoir")
endif()

if(MINGW)
    target_link_options(echoir-cli PRIVATE -static -static-libgcc -static-libstdc++)
    if(NPM_EXECUTABLE)
        target_link_libraries(echoir-cli PRIVATE ws2_32)
    endif()
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
