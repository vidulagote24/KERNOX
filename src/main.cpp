#include "kernox/cli/command.hpp"

int main(int argc, char* argv[]) {
    return kernox::cli::CommandDispatcher::run(argc, argv);
}
