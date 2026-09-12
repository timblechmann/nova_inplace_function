cmake_minimum_required(VERSION 3.25)

project(nova_inplace_function VERSION 0.1.0 LANGUAGES CXX)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

if (PROJECT_IS_TOP_LEVEL)
    add_custom_target(nova_inplace_function_project_files SOURCES
        .clang-tidy
        .pre-commit-config.yaml
        _clang-format
        README.md
        LICENSE
        .github/workflows/ci.yml
    )
endif()

# Project layout
add_library(nova_inplace_function INTERFACE)
add_library(nova::inplace_function ALIAS nova_inplace_function)

target_sources(nova_inplace_function PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/nova/inplace_function.hpp)

target_include_directories(nova_inplace_function INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Testing
option(NOVA_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
if(NOVA_BUILD_TESTS)
    if (NOT COMMAND CPMAddPackage)
        set(CPM_DOWNLOAD_VERSION 0.43.1)
        set(CPM_DOWNLOAD_HASH 1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934)
        set(_cpm_file "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
        if (EXISTS "${_cpm_file}")
            # A stale or partial cached copy must not fail configures forever:
            # verify it and re-download on mismatch.
            file(SHA256 "${_cpm_file}" _cpm_actual_hash)
            if (NOT _cpm_actual_hash STREQUAL CPM_DOWNLOAD_HASH)
                message(STATUS "Cached CPM.cmake hash mismatch, re-downloading...")
                file(REMOVE "${_cpm_file}")
            endif()
        endif()
        if (NOT EXISTS "${_cpm_file}")
            message(STATUS "Downloading CPM.cmake ${CPM_DOWNLOAD_VERSION}...")
            file(DOWNLOAD
                "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
                "${_cpm_file}"
                TLS_VERIFY ON
                EXPECTED_HASH SHA256=${CPM_DOWNLOAD_HASH}
                STATUS _cpm_download_status
            )
            list(GET _cpm_download_status 0 _cpm_download_error)
            if (_cpm_download_error)
                # Drop the partial file so the next configure retries cleanly.
                file(REMOVE "${_cpm_file}")
                list(GET _cpm_download_status 1 _cpm_download_message)
                message(FATAL_ERROR "Failed to download CPM.cmake: ${_cpm_download_message}")
            endif()
        endif()
        include("${_cpm_file}")
    endif()

    if (NOT TARGET Catch2::Catch2WithMain)
        block()
            set(CMAKE_UNITY_BUILD ON)
            CPMAddPackage(
                NAME Catch2
                GITHUB_REPOSITORY catchorg/Catch2
                VERSION 3.16.0
            )
        endblock()
    endif()

    if (DEFINED Catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    endif()
    include(Catch)

    add_executable(nova_inplace_function_tests
        tests/test_inplace_function.cpp
        tests/test_move_only_inplace_function.cpp
    )
    target_link_libraries(nova_inplace_function_tests PRIVATE nova::inplace_function Catch2::Catch2WithMain)

    enable_testing()
    catch_discover_tests(nova_inplace_function_tests)

    # Freestanding smoke tests (standalone: Catch2 headers need RTTI/EH)
    add_executable(nova_inplace_function_no_rtti_tests tests/no_rtti.cpp)
    target_link_libraries(nova_inplace_function_no_rtti_tests PRIVATE nova::inplace_function)
    if(MSVC)
        target_compile_options(nova_inplace_function_no_rtti_tests PRIVATE /GR-)
    else()
        target_compile_options(nova_inplace_function_no_rtti_tests PRIVATE -fno-rtti)
    endif()
    add_test(NAME nova_inplace_function_no_rtti_tests COMMAND nova_inplace_function_no_rtti_tests)

    add_executable(nova_inplace_function_no_exceptions_tests tests/no_exceptions.cpp)
    target_link_libraries(nova_inplace_function_no_exceptions_tests PRIVATE nova::inplace_function)
    # CMake drops function-style -D definitions from the command line, so the
    # abort-based THROW macro comes from a configured header instead.
    configure_file(tests/no_exceptions_config.h.in no_exceptions_config.h @ONLY)
    target_include_directories(nova_inplace_function_no_exceptions_tests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
    )
    if(NOT MSVC)
        # MSVC keeps the throwing default (see header rationale); still a useful API smoke test
        target_compile_options(nova_inplace_function_no_exceptions_tests PRIVATE -fno-exceptions)
    endif()
    add_test(NAME nova_inplace_function_no_exceptions_tests COMMAND nova_inplace_function_no_exceptions_tests)

    # Negative test: enabled introspection without RTTI must not compile.
    # Built on demand by ctest itself; failure to build is the expected outcome.
    add_executable(nova_inplace_function_no_rtti_rejected EXCLUDE_FROM_ALL tests/no_rtti_enabled_rejected.cpp)
    target_link_libraries(nova_inplace_function_no_rtti_rejected PRIVATE nova::inplace_function)
    if(MSVC)
        target_compile_options(nova_inplace_function_no_rtti_rejected PRIVATE /GR-)
    else()
        target_compile_options(nova_inplace_function_no_rtti_rejected PRIVATE -fno-rtti)
    endif()
    add_test(NAME nova_inplace_function_no_rtti_rejected
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target nova_inplace_function_no_rtti_rejected --config $<CONFIG>
    )
    set_tests_properties(nova_inplace_function_no_rtti_rejected PROPERTIES WILL_FAIL TRUE)

    # Negative tests: compile-time rejections must fail to compile.
    # Built on demand by ctest itself; failure to build is the expected outcome.
    set(nova_inplace_function_negative_tests
        oversized_closure_rejected
        overaligned_closure_rejected
        move_only_closure_rejected
        throwing_move_closure_rejected
        narrowing_capacity_rejected
    )
    foreach(_neg_test IN LISTS nova_inplace_function_negative_tests)
        add_executable(nova_inplace_function_neg_${_neg_test} EXCLUDE_FROM_ALL tests/negative/${_neg_test}.cpp)
        target_link_libraries(nova_inplace_function_neg_${_neg_test} PRIVATE nova::inplace_function)
        add_test(NAME nova_inplace_function_neg_${_neg_test}
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target nova_inplace_function_neg_${_neg_test} --config $<CONFIG>
        )
        set_tests_properties(nova_inplace_function_neg_${_neg_test} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS nova_inplace_function
    EXPORT nova_inplace_functionTargets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT nova_inplace_functionTargets
    FILE nova_inplace_functionTargets.cmake
    NAMESPACE nova::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nova_inplace_function
)
