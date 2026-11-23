#pragma once
// Minimal std::optional replacement for compilers without C++17 <optional>
// Compatible with GCC 6.x (MinGW 6.3)
#include <utility>
#include <stdexcept>

#if __cplusplus >= 201703L && !defined(__MINGW32__)
  #include <optional>
#else
// Simple optional implementation for older compilers
namespace std {

struct nullopt_t { explicit nullopt_t() = default; };
constexpr nullopt_t nullopt{};

template<typename T>
class optional {
public:
    optional() : has_val_(false) {}
    optional(nullopt_t) : has_val_(false) {}
    optional(const T& v) : val_(v), has_val_(true) {}
    optional(T&& v) : val_(std::move(v)), has_val_(true) {}

    bool has_value() const { return has_val_; }
    explicit operator bool() const { return has_val_; }

    T& value() { return val_; }
    const T& value() const { return val_; }

    T& operator*() { return val_; }
    const T& operator*() const { return val_; }

    T* operator->() { return &val_; }
    const T* operator->() const { return &val_; }

    void reset() { has_val_ = false; }

private:
    T val_{};
    bool has_val_;
};

} // namespace std
#endif
