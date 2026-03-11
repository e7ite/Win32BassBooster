cmake_minimum_required(VERSION 3.16)
project(Win32BassBooster VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Register the project's git hooks directory so clang-format runs on commit.
# This is a one-time local git config; it does not affect the remote.
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_GUI   "Build Win32 GUI application (Windows only)" ON)

# -----------------------------------------------------------------------
# Platform detection
# -----------------------------------------------------------------------
if(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        add_compile_options(/W4 /WX /utf-8)
        add_compile_definitions(_WIN32_WINNT=0x0A00)
    else()
        add_compile_options(-Wall -Wextra -Werror)
    endif()
else()
    set(BUILD_GUI OFF)
    message(STATUS
        "Non-Windows: GUI disabled, only DSP core and tests will build")
endif()

# -----------------------------------------------------------------------
# clang-format  (format target; gracefully skipped if not found on PATH)
# -----------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE NAMES clang-format
    HINTS
        "C:/Program Files/LLVM/bin"
        "C:/Program Files (x86)/LLVM/bin"
)
if(CLANG_FORMAT_EXE)
    message(STATUS "clang-format: ${CLANG_FORMAT_EXE}")
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format on all project sources"
    )
else()
    message(WARNING
        "clang-format not found on PATH; the 'format' target and pre-commit "
        "hook will be unavailable.  Install LLVM and ensure clang-format is "
        "on PATH, or pass -DENABLE_CLANG_TIDY=OFF to silence the clang-tidy "
        "warning as well.  See the 'Code style' section of README.md.")
endif()

# -----------------------------------------------------------------------
# clang-tidy (on by default; gracefully skipped if not found on PATH)
# -----------------------------------------------------------------------
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis on project sources" ON)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy
        HINTS
            "C:/Program Files/LLVM/bin"
            "C:/Program Files (x86)/LLVM/bin"
    )
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING
            "ENABLE_CLANG_TIDY=ON but clang-tidy was not found on PATH; "
            "analysis will be skipped.  Install LLVM or pass "
            "-DENABLE_CLANG_TIDY=OFF to silence this warning.")
    endif()
endif()

# Apply clang-tidy to a first-party target.
# Google Test and other FetchContent targets are intentionally excluded.
function(target_enable_clang_tidy tgt)
    if(ENABLE_CLANG_TIDY AND CLANG_TIDY_EXE)
        set_target_properties(${tgt} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled for target: ${tgt}")
    endif()
endfunction()

# -----------------------------------------------------------------------
# Google Test
# -----------------------------------------------------------------------
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(CTest)
    include(GoogleTest)
endif()

# -----------------------------------------------------------------------
# DSP static libraries  (cross-platform)
# -----------------------------------------------------------------------
add_library(bass_boost_filter STATIC src/bass_boost_filter.cpp)
target_include_directories(bass_boost_filter PUBLIC src)
if(MSVC)
    target_compile_options(bass_boost_filter PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(bass_boost_filter PRIVATE -O2)
endif()
target_enable_clang_tidy(bass_boost_filter)

add_library(harmonic_exciter STATIC src/harmonic_exciter.cpp)
target_include_directories(harmonic_exciter PUBLIC src)
if(MSVC)
    target_compile_options(harmonic_exciter PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(harmonic_exciter PRIVATE -O2)
endif()
target_enable_clang_tidy(harmonic_exciter)

# -----------------------------------------------------------------------
# Win32-only static libraries
# -----------------------------------------------------------------------
if(WIN32)
    add_library(endpoint_audio_format STATIC src/endpoint_audio_format.cpp)
    target_include_directories(endpoint_audio_format PUBLIC src)
    target_enable_clang_tidy(endpoint_audio_format)

    add_library(audio_pipeline STATIC src/audio_pipeline.cpp)
    target_include_directories(audio_pipeline PUBLIC src)
    target_link_libraries(audio_pipeline PUBLIC
        endpoint_audio_format bass_boost_filter harmonic_exciter
        ole32 oleaut32 uuid avrt)
    target_enable_clang_tidy(audio_pipeline)
endif()

# -----------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------
if(BUILD_TESTS)
    # --- Cross-platform DSP tests ---
    add_executable(bass_boost_filter_test src/bass_boost_filter_test.cpp)
    target_link_libraries(bass_boost_filter_test PRIVATE
        bass_boost_filter GTest::gtest_main)
    target_include_directories(bass_boost_filter_test PRIVATE src)
    add_test(NAME bass_boost_filter_test
             COMMAND $<TARGET_FILE:bass_boost_filter_test> --gtest_color=yes)
    target_enable_clang_tidy(bass_boost_filter_test)

    add_executable(harmonic_exciter_test src/harmonic_exciter_test.cpp)
    target_link_libraries(harmonic_exciter_test PRIVATE
        harmonic_exciter GTest::gtest_main)
    target_include_directories(harmonic_exciter_test PRIVATE src)
    add_test(NAME harmonic_exciter_test
             COMMAND $<TARGET_FILE:harmonic_exciter_test> --gtest_color=yes)
    target_enable_clang_tidy(harmonic_exciter_test)

    # --- Win32-only tests ---
    if(WIN32)
        add_executable(endpoint_audio_format_test
            src/endpoint_audio_format_test.cpp)
        target_link_libraries(endpoint_audio_format_test PRIVATE
            endpoint_audio_format GTest::gtest_main)
        target_include_directories(endpoint_audio_format_test PRIVATE src)
        add_test(NAME endpoint_audio_format_test
                 COMMAND $<TARGET_FILE:endpoint_audio_format_test> --gtest_color=yes)
        target_enable_clang_tidy(endpoint_audio_format_test)

        add_executable(audio_pipeline_test src/audio_pipeline_test.cpp)
        target_link_libraries(audio_pipeline_test PRIVATE
            audio_pipeline GTest::gtest_main)
        target_include_directories(audio_pipeline_test PRIVATE src)
        add_test(NAME audio_pipeline_test
                 COMMAND $<TARGET_FILE:audio_pipeline_test> --gtest_color=yes)
        target_enable_clang_tidy(audio_pipeline_test)
    endif()
endif()
