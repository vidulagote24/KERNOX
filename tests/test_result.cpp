#include "kernox/core/result.hpp"
#include "test_harness.hpp"

#include <string>

using namespace kernox::core;

int main() {
    // 1. Successful Result<int>
    {
        Result<int> res = Result<int>::ok(100);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_FALSE(res.is_err());
        KERNOX_ASSERT_TRUE(res.has_value());
        KERNOX_ASSERT_TRUE(static_cast<bool>(res));
        KERNOX_ASSERT_EQ(res.value(), 100);
        KERNOX_ASSERT_EQ(res.value_or(200), 100);

        // Accessing error() on success throws std::logic_error
        KERNOX_ASSERT_THROWS(res.error(), std::logic_error);
    }

    // 2. Failed Result<int>
    {
        Error err(ErrorCode::NotFound, "Item not found in lookup", 2);
        Result<int> res = Result<int>::err(err);
        KERNOX_ASSERT_FALSE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_FALSE(res.has_value());
        KERNOX_ASSERT_FALSE(static_cast<bool>(res));
        KERNOX_ASSERT_EQ(res.value_or(999), 999);

        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::NotFound);
        KERNOX_ASSERT_EQ(res.error().message, "Item not found in lookup");
        KERNOX_ASSERT_EQ(res.error().system_errno, 2);

        // Accessing value() on failure throws std::logic_error
        KERNOX_ASSERT_THROWS(res.value(), std::logic_error);
    }

    // 3. Move semantics with non-trivial type
    {
        std::string payload = "Hello Kernox";
        Result<std::string> res = Result<std::string>::ok(std::move(payload));
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(res.value(), "Hello Kernox");

        std::string extracted = std::move(res).value();
        KERNOX_ASSERT_EQ(extracted, "Hello Kernox");
    }

    // 4. Result<void> success
    {
        Result<void> res = Result<void>::ok();
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_FALSE(res.is_err());
        KERNOX_ASSERT_TRUE(res.has_value());
        KERNOX_ASSERT_THROWS(res.error(), std::logic_error);
    }

    // 5. Result<void> failure
    {
        Result<void> res = Result<void>::err(ErrorCode::PermissionDenied, "Access denied");
        KERNOX_ASSERT_FALSE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::PermissionDenied);
        KERNOX_ASSERT_THROWS(res.value(), std::logic_error);
    }

    std::cout << "[PASS] test_result completed successfully.\n";
    return 0;
}
