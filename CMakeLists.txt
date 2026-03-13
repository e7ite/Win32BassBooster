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
option(BUILD_TESTS  "Build unit tests" ON)
option(BUILD_GUI    "Build Win32 GUI application" ON)
option(ENABLE_ASAN  "Enable AddressSanitizer (MSVC /fsanitize=address)" OFF)

# -----------------------------------------------------------------------
# Windows configuration
# -----------------------------------------------------------------------
if(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        add_compile_options(/W4 /WX /utf-8)
        if(ENABLE_ASAN)
            add_compile_options(/fsanitize=address)
        endif()
        add_compile_definitions(_WIN32_WINNT=0x0A00)
    else()
        add_compile_options(-Wall -Wextra -Werror)
        if(ENABLE_ASAN)
            add_compile_options(-fsanitize=address)
            add_link_options(-fsanitize=address)
        endif()
    endif()
else()
    set(BUILD_GUI OFF)
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
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(CTest)
    include(GoogleTest)
endif()

# -----------------------------------------------------------------------
# DSP static libraries
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

# -----------------------------------------------------------------------
# Audio and UI static libraries
# -----------------------------------------------------------------------
if(WIN32)
    add_library(endpoint_audio_format STATIC src/endpoint_audio_format.cpp)
    target_include_directories(endpoint_audio_format PUBLIC src)
    target_enable_clang_tidy(endpoint_audio_format)

    add_library(theme_manager STATIC src/theme_manager.cpp)
    target_include_directories(theme_manager PUBLIC src)
    target_link_libraries(theme_manager PUBLIC dwmapi uxtheme)
    target_enable_clang_tidy(theme_manager)

    add_library(loopback_capture_activation STATIC
        src/loopback_capture_activation.cpp)
    target_include_directories(loopback_capture_activation PUBLIC src)
    target_link_libraries(loopback_capture_activation PUBLIC
        ole32 oleaut32 uuid mmdevapi)
    target_enable_clang_tidy(loopback_capture_activation)

    add_library(wasapi_audio_device STATIC src/wasapi_audio_device.cpp)
    target_include_directories(wasapi_audio_device PUBLIC src)
    target_link_libraries(wasapi_audio_device PUBLIC
        endpoint_audio_format loopback_capture_activation
        ole32 oleaut32 uuid mmdevapi)
    target_enable_clang_tidy(wasapi_audio_device)

    add_library(audio_pipeline STATIC src/audio_pipeline.cpp)
    target_include_directories(audio_pipeline PUBLIC src)
    target_link_libraries(audio_pipeline PUBLIC
        endpoint_audio_format bass_boost_filter
        loopback_capture_activation
        ole32 oleaut32 uuid avrt mmdevapi)
    target_enable_clang_tidy(audio_pipeline)

    add_library(main_window STATIC src/main_window.cpp)
    target_include_directories(main_window PUBLIC src)
    target_link_libraries(main_window PUBLIC
        audio_pipeline theme_manager
        uxtheme dwmapi winmm comctl32)
    target_enable_clang_tidy(main_window)
endif()

# -----------------------------------------------------------------------
# Main executable
# -----------------------------------------------------------------------
if(WIN32 AND BUILD_GUI)
    add_executable(Win32BassBooster WIN32
        src/main.cpp
        resources/app.rc
    )
    target_include_directories(Win32BassBooster PRIVATE src)
    target_link_libraries(Win32BassBooster PRIVATE
        main_window audio_pipeline theme_manager
        bass_boost_filter
        uxtheme dwmapi avrt ole32 oleaut32 uuid winmm)
    if(MSVC)
        target_compile_options(Win32BassBooster PRIVATE /W4 /WX)
        set_target_properties(Win32BassBooster PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /MANIFEST:NO"
        )
    endif()
    target_enable_clang_tidy(Win32BassBooster)
endif()

# -----------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------
if(BUILD_TESTS)
    # --- DSP tests ---
    add_executable(bass_boost_filter_test src/bass_boost_filter_test.cpp)
    target_link_libraries(bass_boost_filter_test PRIVATE
        bass_boost_filter GTest::gtest_main)
    target_include_directories(bass_boost_filter_test PRIVATE src)
    add_test(NAME bass_boost_filter_test
             COMMAND $<TARGET_FILE:bass_boost_filter_test> --gtest_color=yes)
    target_enable_clang_tidy(bass_boost_filter_test)

    # --- Windows integration tests ---
    if(WIN32)
        add_executable(endpoint_audio_format_test
            src/endpoint_audio_format_test.cpp)
        target_link_libraries(endpoint_audio_format_test PRIVATE
            endpoint_audio_format GTest::gtest_main)
        target_include_directories(endpoint_audio_format_test PRIVATE src)
        add_test(NAME endpoint_audio_format_test
                 COMMAND $<TARGET_FILE:endpoint_audio_format_test> --gtest_color=yes)
        target_enable_clang_tidy(endpoint_audio_format_test)

        add_executable(loopback_capture_activation_test
            src/loopback_capture_activation_test.cpp)
        target_link_libraries(loopback_capture_activation_test PRIVATE
            loopback_capture_activation GTest::gtest_main)
        target_include_directories(loopback_capture_activation_test PRIVATE src)
        add_test(NAME loopback_capture_activation_test
                 COMMAND $<TARGET_FILE:loopback_capture_activation_test> --gtest_color=yes)
        target_enable_clang_tidy(loopback_capture_activation_test)

        add_executable(audio_pipeline_test src/audio_pipeline_test.cpp)
        target_link_libraries(audio_pipeline_test PRIVATE
            audio_pipeline GTest::gtest_main)
        target_include_directories(audio_pipeline_test PRIVATE src)
        add_test(NAME audio_pipeline_test
                 COMMAND $<TARGET_FILE:audio_pipeline_test> --gtest_color=yes)
        target_enable_clang_tidy(audio_pipeline_test)

        add_executable(wasapi_audio_device_test
            src/wasapi_audio_device_test.cpp)
        target_link_libraries(wasapi_audio_device_test PRIVATE
            wasapi_audio_device GTest::gtest_main)
        target_include_directories(wasapi_audio_device_test PRIVATE src)
        add_test(NAME wasapi_audio_device_test
                 COMMAND $<TARGET_FILE:wasapi_audio_device_test>
                         --gtest_color=yes)
        target_enable_clang_tidy(wasapi_audio_device_test)

        add_executable(theme_manager_test src/theme_manager_test.cpp)
        target_link_libraries(theme_manager_test PRIVATE
            theme_manager GTest::gtest_main GTest::gmock)
        target_include_directories(theme_manager_test PRIVATE src)
        add_test(NAME theme_manager_test
                 COMMAND $<TARGET_FILE:theme_manager_test> --gtest_color=yes)
        target_enable_clang_tidy(theme_manager_test)

        add_executable(main_window_test src/main_window_test.cpp)
        target_link_libraries(main_window_test PRIVATE
            main_window GTest::gtest_main)
        target_include_directories(main_window_test PRIVATE src)
        add_test(NAME main_window_test
                 COMMAND $<TARGET_FILE:main_window_test> --gtest_color=yes)
        target_enable_clang_tidy(main_window_test)
    endif()
endif()
