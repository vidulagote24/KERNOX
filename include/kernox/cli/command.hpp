#pragma once

#include <string>
#include <vector>

namespace kernox::cli {

class CommandDispatcher {
public:
    // Dispatches command line arguments and returns exit code (0 for success, non-zero for error)
    static int run(int argc, char* argv[]);
    static int run(const std::vector<std::string>& args);

    static void print_help();
    static void print_version();
    static int execute_status();
    static int execute_self_test();
    static int execute_schedule(const std::vector<std::string>& args);
    static int execute_disk_schedule(const std::vector<std::string>& args);
    static int execute_deadlock(const std::vector<std::string>& args);
    static int execute_memory(const std::vector<std::string>& args);
    static int execute_process(const std::vector<std::string>& args);
    static int execute_ipc(const std::vector<std::string>& args);
};

} // namespace kernox::cli
