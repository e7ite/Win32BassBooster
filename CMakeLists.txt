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
