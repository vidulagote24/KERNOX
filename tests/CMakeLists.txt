# tests/CMakeLists.txt

macro(kernox_add_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(${test_name} PRIVATE
        kernox_cli
        kernox_system
        kernox_sim
        kernox_core
        kernox_compiler_flags
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

kernox_add_test(test_result test_result.cpp)
kernox_add_test(test_timer test_timer.cpp)
kernox_add_test(test_unique_fd test_unique_fd.cpp)
kernox_add_test(test_child_process test_child_process.cpp)
kernox_add_test(test_cli test_cli.cpp)
kernox_add_test(test_cpu_sched test_cpu_sched.cpp)
kernox_add_test(test_disk_sched test_disk_sched.cpp)
kernox_add_test(test_deadlock test_deadlock.cpp)
kernox_add_test(test_memory test_memory.cpp)
kernox_add_test(test_telemetry test_telemetry.cpp)
kernox_add_test(test_process_manager test_process_manager.cpp)
kernox_add_test(test_ipc test_ipc.cpp)
kernox_add_test(test_shm test_shm.cpp)

add_executable(env_test_helper env_test_helper.cpp)
target_link_libraries(env_test_helper PRIVATE kernox_compiler_flags)
