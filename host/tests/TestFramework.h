#pragma once

// Minimal self-registering test framework, to avoid an external dependency.

#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vdrum::test
{

struct TestCase
{
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

struct Registrar
{
    Registrar (const char* name, std::function<void()> body)
    {
        registry().push_back ({ name, std::move (body) });
    }
};

inline void reportFailure (const char* expression, const char* file, int line)
{
    ++failureCount();
    std::printf ("  FAILED: %s (%s:%d)\n", expression, file, line);
}

inline int runAll()
{
    int failedTests = 0;
    for (const auto& test : registry())
    {
        const auto before = failureCount();
        test.body();
        const auto passed = failureCount() == before;
        std::printf ("[%s] %s\n", passed ? " OK " : "FAIL", test.name);
        if (! passed)
            ++failedTests;
    }

    std::printf ("\n%d test(s), %d failed\n", static_cast<int> (registry().size()), failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace vdrum::test

#define VDRUM_TEST(name)                                                              \
    static void name();                                                               \
    static const vdrum::test::Registrar name##Registrar { #name, name };              \
    static void name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (! (condition))                                                            \
            vdrum::test::reportFailure (#condition, __FILE__, __LINE__);              \
    } while (false)

// Stops the current test when the condition fails (e.g. before dereferencing).
#define REQUIRE(condition)                                                            \
    do {                                                                              \
        if (! (condition))                                                            \
        {                                                                             \
            vdrum::test::reportFailure (#condition, __FILE__, __LINE__);              \
            return;                                                                   \
        }                                                                             \
    } while (false)
