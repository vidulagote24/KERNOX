#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>

#define KERNOX_ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Assertion failed: (" #cond ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

#define KERNOX_ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            std::cerr << "[FAIL] Assertion failed: !(" #cond ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

#define KERNOX_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "[FAIL] Assertion failed: (" #a " == " #b ") [" << (a) << " != " << (b) \
                      << "] at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

#define KERNOX_ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "[FAIL] Assertion failed: (" #a " != " #b ") [" << (a) << " == " << (b) \
                      << "] at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

#define KERNOX_ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool thrown = false; \
        try { \
            static_cast<void>(expr); \
        } catch (const ExceptionType&) { \
            thrown = true; \
        } catch (...) { \
            std::cerr << "[FAIL] Unexpected exception type at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
        if (!thrown) { \
            std::cerr << "[FAIL] Expected exception " #ExceptionType " not thrown at " \
                      << __FILE__ << ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)
