cmake_minimum_required(VERSION 3.20)

project(xormap_image_rgb565_matlab_cpp VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(TIFF REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO_PDF REQUIRED IMPORTED_TARGET cairo-pdf)

set(GRAYSCALE_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xormap_image_matlab_cpp"
    CACHE PATH "Path to the shared grayscale C++ implementation")
set(COLOR_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xormap_image_rgb888_matlab_cpp"
    CACHE PATH "Path to the shared packed-colour C++ implementation")

add_library(xormap_color_core
    ${COLOR_CPP_DIR}/src/common.cpp
    ${GRAYSCALE_CPP_DIR}/src/core.cpp
    ${GRAYSCALE_CPP_DIR}/src/image.cpp
    ${GRAYSCALE_CPP_DIR}/src/metrics.cpp
    ${GRAYSCALE_CPP_DIR}/src/plot.cpp
    ${GRAYSCALE_CPP_DIR}/src/threefry.cpp
)
add_library(xormap_color::core ALIAS xormap_color_core)
target_compile_features(xormap_color_core PUBLIC cxx_std_17)
target_include_directories(xormap_color_core PUBLIC
    $<BUILD_INTERFACE:${COLOR_CPP_DIR}/include>
    $<BUILD_INTERFACE:${GRAYSCALE_CPP_DIR}/include>
)
target_link_libraries(xormap_color_core PUBLIC
    OpenSSL::Crypto TIFF::TIFF Threads::Threads PkgConfig::CAIRO_PDF
)

add_library(xormap_rgb565 src/evaluation.cpp)
add_library(xormap_rgb565::xormap_rgb565 ALIAS xormap_rgb565)
target_compile_features(xormap_rgb565 PUBLIC cxx_std_17)
target_include_directories(xormap_rgb565 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(xormap_rgb565 PUBLIC xormap_color::core)

add_executable(xormap_rgb565_cli apps/xormap_rgb565.cpp)
set_target_properties(xormap_rgb565_cli PROPERTIES OUTPUT_NAME xormap_rgb565)
target_link_libraries(xormap_rgb565_cli PRIVATE xormap_rgb565::xormap_rgb565)

foreach(target xormap_color_core xormap_rgb565 xormap_rgb565_cli)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
endforeach()

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
    add_executable(xormap_rgb565_tests tests/test_rgb565.cpp)
    target_link_libraries(xormap_rgb565_tests PRIVATE
        xormap_rgb565::xormap_rgb565 Catch2::Catch2WithMain
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(xormap_rgb565_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    catch_discover_tests(xormap_rgb565_tests
        TEST_PREFIX "rgb565::"
        PROPERTIES LABELS "unit"
    )
    add_test(NAME rgb565::cli_help COMMAND xormap_rgb565_cli help)
    set_tests_properties(rgb565::cli_help PROPERTIES LABELS "smoke;cli")
endif()

install(TARGETS xormap_color_core xormap_rgb565 xormap_rgb565_cli)
install(DIRECTORY include/ DESTINATION include)
