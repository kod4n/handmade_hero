#pragma once

// Runs code when the enclosing scope exits, e.g.
//     HANDLE file = CreateFile(...);
//     defer(CloseHandle(file));
template <typename F>
struct Defer {
    F f;
    ~Defer() {
        f();
    }
};

#define DEFER_CONCAT_(a, b) a##b
#define DEFER_CONCAT(a, b) DEFER_CONCAT_(a, b)
#define defer(code)                        \
    Defer DEFER_CONCAT(defer_, __LINE__) { \
        [&] { code; }                      \
    }
