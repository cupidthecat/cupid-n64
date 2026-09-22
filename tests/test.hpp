#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace test {

struct Case {
    std::string name;
    void (*run)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> entries;
    return entries;
}

struct Register {
    Register(const char* name, void (*run)()) {
        cases().push_back({name, run});
    }
};

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expression);
    }
}

template <class T> std::string describe(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_enum_v<T>) {
        out << static_cast<std::underlying_type_t<T>>(value);
    } else if constexpr (std::is_integral_v<T>) {
        out << +value << " (0x" << std::hex << +value << ')';
    } else if constexpr (requires { out << value; }) {
        out << value;
    } else {
        out << "[value]";
    }
    return out.str();
}

template <class A, class B>
void equal(const A& actual, const B& expected, const char* expression, const char* file, int line) {
    if (actual != expected) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expression +
                                 "; got " + describe(actual) + ", expected " + describe(expected));
    }
}

} // namespace test

#define CUPID_JOIN_INNER(a, b) a##b
#define CUPID_JOIN(a, b) CUPID_JOIN_INNER(a, b)
#define TEST(name)                                                                                           \
    static void CUPID_JOIN(test_case_, __LINE__)();                                                          \
    static const ::test::Register CUPID_JOIN(test_registration_,                                             \
                                             __LINE__)(#name, &CUPID_JOIN(test_case_, __LINE__));            \
    static void CUPID_JOIN(test_case_, __LINE__)()
#define CHECK(expression) ::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected)                                                                           \
    ::test::equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
