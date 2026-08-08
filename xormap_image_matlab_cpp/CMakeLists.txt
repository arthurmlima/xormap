cmake_minimum_required(VERSION 3.20)

project(xormap_image_matlab_cpp VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(TIFF REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO_PDF REQUIRED IMPORTED_TARGET cairo-pdf)

add_library(xormap_image
    src/core.cpp
    src/download.cpp
    src/evaluation.cpp
    src/image.cpp
    src/metrics.cpp
    src/plot.cpp
    src/threefry.cpp
)
add_library(xormap_image::xormap_image ALIAS xormap_image)

target_compile_features(xormap_image PUBLIC cxx_std_17)
target_include_directories(xormap_image
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(xormap_image
    PUBLIC
        OpenSSL::Crypto
        TIFF::TIFF
        CURL::libcurl
        PkgConfig::CAIRO_PDF
        Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(xormap_image PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(xormap_gray apps/xormap_gray.cpp)
target_link_libraries(xormap_gray PRIVATE xormap_image::xormap_image)
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(xormap_gray PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    add_executable(xormap_image_tests
        tests/test_core.cpp
        tests/test_download.cpp
        tests/test_image.cpp
        tests/test_metrics.cpp
        tests/test_parallel.cpp
        tests/test_threefry.cpp
    )
    target_link_libraries(xormap_image_tests
        PRIVATE xormap_image::xormap_image Catch2::Catch2WithMain
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(xormap_image_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    catch_discover_tests(xormap_image_tests
        TEST_PREFIX "unit::"
        PROPERTIES LABELS "unit"
    )

    add_executable(xormap_image_integration_tests
        tests/test_evaluation.cpp
        tests/test_plot.cpp
    )
    target_link_libraries(xormap_image_integration_tests
        PRIVATE xormap_image::xormap_image Catch2::Catch2WithMain
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(xormap_image_integration_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    catch_discover_tests(xormap_image_integration_tests
        TEST_PREFIX "integration::"
        PROPERTIES LABELS "integration"
    )

    add_test(
        NAME integration::matlab_fast_path
        COMMAND xormap_gray verify
    )
    set_tests_properties(integration::matlab_fast_path PROPERTIES LABELS "integration")

    add_test(NAME smoke::cli_help COMMAND xormap_gray help)
    set_tests_properties(smoke::cli_help PROPERTIES
        LABELS "smoke;cli"
        PASS_REGULAR_EXPRESSION "Usage:"
    )

    add_test(
        NAME smoke::cli_reject_verify_option
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:xormap_gray>
            -DCLI_SUBCOMMAND=verify
            -DCLI_OPTION=--images
            -DCLI_VALUE=bogus
            "-DEXPECTED=verify does not accept --images"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli_failure.cmake
    )
    set_tests_properties(smoke::cli_reject_verify_option PROPERTIES
        LABELS "smoke;cli"
    )

    add_test(
        NAME smoke::cli_reject_run_all_option
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:xormap_gray>
            -DCLI_SUBCOMMAND=run-all
            -DCLI_OPTION=--k-last
            -DCLI_VALUE=24
            "-DEXPECTED=run-all does not accept --k-last"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli_failure.cmake
    )
    set_tests_properties(smoke::cli_reject_run_all_option PROPERTIES
        LABELS "smoke;cli"
    )
endif()

install(TARGETS xormap_image xormap_gray)
install(DIRECTORY include/ DESTINATION include)
