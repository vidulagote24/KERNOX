# tests/CMakeLists.txt

macro(kernox_add_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
    )
    target_compile_definitions(${test_name} PRIVATE
        KERNOX_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/adb"
    )
    target_link_libraries(${test_name} PRIVATE
        kernox_cli
        kernox_simulator
        kernox_adb
        kernox_experiment
        kernox_reclaim
        kernox_intel
        kernox_system
        kernox_core
        kernox_compiler_flags
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

# Core Foundations
kernox_add_test(test_result test_result.cpp)
kernox_add_test(test_timer test_timer.cpp)
kernox_add_test(test_unique_fd test_unique_fd.cpp)
kernox_add_test(test_child_process test_child_process.cpp)

# System Telemetry & Status
kernox_add_test(test_telemetry test_telemetry.cpp)

# Android Process Intelligence
kernox_add_test(test_process_intel test_process_intel.cpp)

# Memory Reclamation Policies & Engine
kernox_add_test(test_reclamation test_reclamation.cpp)

# Experiment & Benchmark Comparisons
kernox_add_test(test_experiment test_experiment.cpp)

# Android ADB Observation & Parsing
kernox_add_test(test_adb_parser test_adb_parser.cpp)

# Interactive Android Simulator & Analytics Bridge
kernox_add_test(test_interactive_simulator test_interactive_simulator.cpp)

# CLI Command Dispatcher
kernox_add_test(test_cli test_cli.cpp)
