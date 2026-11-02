#include <cstdlib>
#include <cstring>

// Helper binary for process manager tests.
// Usage: env_test_helper <VAR_NAME> <EXPECTED_VALUE>
// Exit codes:
// 0: Variable exists and its value matches EXPECTED_VALUE exactly.
// 2: Invalid argument count.
// 3: Variable does not exist in environment.
// 4: Variable exists but its value does not match EXPECTED_VALUE.
int main(int argc, char* argv[]) {
    if (argc != 3) {
        return 2;
    }
    const char* var_name = argv[1];
    const char* expected_value = argv[2];
    const char* actual_value = std::getenv(var_name);
    if (actual_value == nullptr) {
        return 3;
    }
    if (std::strcmp(actual_value, expected_value) != 0) {
        return 4;
    }
    return 0;
}
