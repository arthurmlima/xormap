cmake_minimum_required(VERSION 3.20)

project(xormap_image_rgb888_matlab_cpp VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(TIFF REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO_PDF REQUIRED IMPORTED_TARGET cairo-pdf)

set(GRAYSCALE_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xormap_image_matlab_cpp"
    CACHE PATH "Path to the shared grayscale C++ implementation")

add_library(xormap_color_core
    src/common.cpp
    ${GRAYSCALE_CPP_DIR}/src/core.cpp
    ${GRAYSCALE_CPP_DIR}/src/image.cpp
    ${GRAYSCALE_CPP_DIR}/src/metrics.cpp
    ${GRAYSCALE_CPP_DIR}/src/plot.cpp
    ${GRAYSCALE_CPP_DIR}/src/threefry.cpp
)
add_library(xormap_color::core ALIAS xormap_color_core)
target_compile_features(xormap_color_core PUBLIC cxx_std_17)
target_include_directories(xormap_color_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${GRAYSCALE_CPP_DIR}/include>
)
target_link_libraries(xormap_color_core PUBLIC
    OpenSSL::Crypto TIFF::TIFF Threads::Threads PkgConfig::CAIRO_PDF
)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(xormap_color_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
endif()

add_library(xormap_rgb888 src/evaluation.cpp)
add_library(xormap_rgb888::xormap_rgb888 ALIAS xormap_rgb888)
target_compile_features(xormap_rgb888 PUBLIC cxx_std_17)
target_include_directories(xormap_rgb888 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(xormap_rgb888 PUBLIC xormap_color::core)

add_executable(xormap_rgb888_cli apps/xormap_rgb888.cpp)
set_target_properties(xormap_rgb888_cli PROPERTIES OUTPUT_NAME xormap_rgb888)
target_link_libraries(xormap_rgb888_cli PRIVATE xormap_rgb888::xormap_rgb888)

foreach(target xormap_rgb888 xormap_rgb888_cli)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
endforeach()

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
    add_executable(xormap_rgb888_tests
        tests/test_common.cpp
        tests/test_rgb888.cpp
    )
    target_link_libraries(xormap_rgb888_tests PRIVATE
        xormap_rgb888::xormap_rgb888 Catch2::Catch2WithMain
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(xormap_rgb888_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    catch_discover_tests(xormap_rgb888_tests
        TEST_PREFIX "rgb888::"
        PROPERTIES LABELS "unit"
    )
    add_test(NAME rgb888::cli_help COMMAND xormap_rgb888_cli help)
    set_tests_properties(rgb888::cli_help PROPERTIES LABELS "smoke;cli")
endif()

install(TARGETS xormap_color_core xormap_rgb888 xormap_rgb888_cli)
install(DIRECTORY include/ DESTINATION include)
