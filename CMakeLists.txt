cmake_minimum_required(VERSION 3.25)

project(KERNOX
    VERSION 0.1.0
    DESCRIPTION "A Linux-Based Operating System Resource Management and Benchmarking Platform"
    LANGUAGES C CXX
)

# Standard C++ configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(KERNOX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(KERNOX_BUILD_TESTS "Build KERNOX automated test suite" ON)
set(KERNOX_SANITIZER "none" CACHE STRING "Sanitizer configuration: none, asan, tsan")
set_property(CACHE KERNOX_SANITIZER PROPERTY STRINGS none asan tsan)

# Interface target for compiler warnings and common flags
add_library(kernox_compiler_flags INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kernox_compiler_flags INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
    )
    if(KERNOX_WARNINGS_AS_ERRORS)
        target_compile_options(kernox_compiler_flags INTERFACE -Werror)
    endif()
endif()

# Sanitizer configuration
if(KERNOX_SANITIZER STREQUAL "asan")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "KERNOX: Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
        target_compile_options(kernox_compiler_flags INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(kernox_compiler_flags INTERFACE
            -fsanitize=address,undefined
        )
    else()
        message(WARNING "Sanitizers requested but compiler ${CMAKE_CXX_COMPILER_ID} is not supported.")
    endif()
elseif(KERNOX_SANITIZER STREQUAL "tsan")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "KERNOX: Enabling ThreadSanitizer")
        target_compile_options(kernox_compiler_flags INTERFACE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(kernox_compiler_flags INTERFACE
            -fsanitize=thread
        )
    else()
        message(WARNING "ThreadSanitizer requested but compiler ${CMAKE_CXX_COMPILER_ID} is not supported.")
    endif()
elseif(NOT KERNOX_SANITIZER STREQUAL "none")
    message(FATAL_ERROR "Unknown KERNOX_SANITIZER option: '${KERNOX_SANITIZER}'. Valid options: none, asan, tsan.")
endif()

# -----------------------------------------------------------------------------
# Core Library: kernox_core
# -----------------------------------------------------------------------------
add_library(kernox_core STATIC
    src/core/error.cpp
    src/core/unique_fd.cpp
    src/core/child_process.cpp
    src/core/timer.cpp
    src/core/logger.cpp
)

target_include_directories(kernox_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(kernox_core PUBLIC
    kernox_compiler_flags
)

# -----------------------------------------------------------------------------
# Simulation Modules: kernox_sim
# -----------------------------------------------------------------------------
add_library(kernox_sim STATIC
    src/modules/sim/cpu_sched.cpp
    src/modules/sim/disk_sched.cpp
    src/modules/sim/deadlock.cpp
    src/modules/sim/memory.cpp
)

target_include_directories(kernox_sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(kernox_sim PUBLIC
    kernox_core
    kernox_compiler_flags
)

# -----------------------------------------------------------------------------
# System Modules: kernox_system
# -----------------------------------------------------------------------------
add_library(kernox_system STATIC
    src/modules/system/telemetry.cpp
    src/modules/system/process_manager.cpp
    src/modules/system/ipc.cpp
    src/modules/system/shared_memory.cpp
)

target_include_directories(kernox_system PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(kernox_system PUBLIC
    kernox_core
    kernox_compiler_flags
)

# -----------------------------------------------------------------------------
# CLI Library: kernox_cli
# -----------------------------------------------------------------------------
add_library(kernox_cli STATIC
    src/cli/command.cpp
)

target_include_directories(kernox_cli PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(kernox_cli PUBLIC
    kernox_system
    kernox_sim
    kernox_core
    kernox_compiler_flags
)

# -----------------------------------------------------------------------------
# Executable: kernox
# -----------------------------------------------------------------------------
add_executable(kernox
    src/main.cpp
)

target_link_libraries(kernox PRIVATE
    kernox_cli
    kernox_system
    kernox_sim
    kernox_core
    kernox_compiler_flags
)

# -----------------------------------------------------------------------------
# Automated Testing
# -----------------------------------------------------------------------------
if(KERNOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
