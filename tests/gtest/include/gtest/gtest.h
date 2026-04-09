#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace testing {

struct TestInfo {
    std::string suiteName;
    std::string testName;
    std::function<void()> run;
};

inline std::vector<TestInfo>& Registry() {
    static std::vector<TestInfo> tests;
    return tests;
}

inline bool RegisterTest(const char* suiteName, const char* testName, std::function<void()> run) {
    Registry().push_back(TestInfo{suiteName, testName, std::move(run)});
    return true;
}

class TestFailure : public std::exception {
public:
    explicit TestFailure(std::string message) : message_(std::move(message)) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

private:
    std::string message_;
};

inline void InitGoogleTest(int*, char**) {}

inline int RUN_ALL_TESTS() {
    int failures = 0;
    for (const auto& test : Registry()) {
        try {
            test.run();
            std::cout << "[       OK ] " << test.suiteName << "." << test.testName << "\n";
        } catch (const TestFailure& failure) {
            ++failures;
            std::cerr << "[  FAILED  ] " << test.suiteName << "." << test.testName << ": " << failure.what() << "\n";
        } catch (const std::exception& failure) {
            ++failures;
            std::cerr << "[  FAILED  ] " << test.suiteName << "." << test.testName << ": unexpected exception: "
                      << failure.what() << "\n";
        } catch (...) {
            ++failures;
            std::cerr << "[  FAILED  ] " << test.suiteName << "." << test.testName << ": unknown exception\n";
        }
    }
    return failures;
}

template <typename Left, typename Right>
inline void ExpectEqual(const Left& left, const Right& right, const char* leftExpr, const char* rightExpr,
    const char* file, int line, bool fatal) {
    if (!(left == right)) {
        std::ostringstream stream;
        stream << file << ":" << line << " expected " << leftExpr << " == " << rightExpr;
        if (fatal) {
            throw TestFailure(stream.str());
        }
        std::cerr << stream.str() << "\n";
    }
}

inline void ExpectTrue(bool condition, const char* expr, const char* file, int line, bool fatal) {
    if (!condition) {
        std::ostringstream stream;
        stream << file << ":" << line << " expected true: " << expr;
        if (fatal) {
            throw TestFailure(stream.str());
        }
        std::cerr << stream.str() << "\n";
    }
}

}  // namespace testing

#define TEST(suite_name, test_name) \
    void suite_name##_##test_name##_Test(); \
    namespace { \
    const bool suite_name##_##test_name##_registered = \
        ::testing::RegisterTest(#suite_name, #test_name, suite_name##_##test_name##_Test); \
    } \
    void suite_name##_##test_name##_Test()

#define EXPECT_EQ(left, right) ::testing::ExpectEqual((left), (right), #left, #right, __FILE__, __LINE__, false)
#define ASSERT_EQ(left, right) ::testing::ExpectEqual((left), (right), #left, #right, __FILE__, __LINE__, true)
#define EXPECT_TRUE(expr) ::testing::ExpectTrue(static_cast<bool>(expr), #expr, __FILE__, __LINE__, false)
#define ASSERT_TRUE(expr) ::testing::ExpectTrue(static_cast<bool>(expr), #expr, __FILE__, __LINE__, true)
