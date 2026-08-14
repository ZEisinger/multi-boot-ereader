// Copyright (c) multi-boot-ereader contributors. MIT licensed.
//
// A very small assertion/registration helper so the core library can be tested
// on a host without pulling in an external test framework (the firmware build
// itself has no network access to a package registry in CI).
#pragma once

#include <cstddef>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

inline bool registerTest(const std::string& name, const std::function<void()>& body) {
    registry().push_back({name, body});
    return true;
}

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failureCount();
    std::cout << "    FAIL " << file << ":" << line << " " << message << std::endl;
}

inline int runAll() {
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        const int before = failureCount();
        std::cout << "[ RUN ] " << test.name << std::endl;
        test.body();
        if (failureCount() != before) {
            ++failedTests;
            std::cout << "[FAIL ] " << test.name << std::endl;
        } else {
            std::cout << "[  OK ] " << test.name << std::endl;
        }
    }
    std::cout << registry().size() << " tests, " << failedTests << " failed" << std::endl;
    return failedTests == 0 ? 0 : 1;
}

}  // namespace testing

#define TEST(suite, name)                                                                          \
    static void suite##_##name();                                                                  \
    static const bool suite##_##name##_registered = ::testing::registerTest(#suite "." #name, suite##_##name); \
    static void suite##_##name()

#define EXPECT_TRUE(condition)                                                    \
    do {                                                                          \
        if (!(condition)) {                                                       \
            ::testing::reportFailure(__FILE__, __LINE__, "expected: " #condition); \
        }                                                                         \
    } while (false)

#define EXPECT_FALSE(condition) EXPECT_TRUE(!(condition))

#define EXPECT_EQ(actual, expected)                                                        \
    do {                                                                                   \
        const auto& actualValue = (actual);                                                \
        const auto& expectedValue = (expected);                                            \
        if (!(actualValue == expectedValue)) {                                             \
            std::ostringstream message;                                                    \
            message << #actual " == " #expected " (" << actualValue << " vs "              \
                    << expectedValue << ")";                                               \
            ::testing::reportFailure(__FILE__, __LINE__, message.str());                   \
        }                                                                                  \
    } while (false)
