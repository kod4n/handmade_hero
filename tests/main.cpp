// Test runner, and the unity build root for the tests executable. Run it
// through test.bat (Windows) or test.sh (macOS, Linux), which build it first:
//
//   test.bat                 run every test; print failures and a summary
//   test.bat -v              also list every test as it runs, with its check count and time
//   test.bat tile bmp        run only tests whose name contains "tile" or "bmp"
//   test.bat --seed 42       give the random inputs different values
//
// Exit code: 0 if everything passed, 1 if a test failed or nothing matched, 2 on bad arguments.
//
// Platform layers aren't tested: they're mostly OS calls.

#include "test.h"

// ---- Test files ----
// Add new ones here. They come before <windows.h>, which #defines words like
// near, far and small that would break ordinary code.
#include "test_core.cpp"
#include "test_vec.cpp"
#include "test_tile.cpp"
#include "test_entity.cpp"
#include "test_render.cpp"
#include "test_asset.cpp"

// ---- Runner ----

#include <chrono>
#include <csignal>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// Without these, a crash (e.g. a null pointer or a failed assert) ends the
// run with no output, and output still in the buffer is lost. Neither is
// strictly signal-safe, but the process is going down anyway.
static void print_crash_location(const char* what) {
    const Test* test = g_test_state.current;
    if (test) {
        std::printf("CRASH %s: %s\n", test->name, what);
        std::printf("%s:%d: error: test crashed\n", test->file, test->line);
    } else {
        std::printf("CRASH outside any test: %s\n", what);
    }
    std::fflush(stdout);
}

// assert() calls abort(), which raises SIGABRT on every platform.
static void on_crash_signal(int signal) {
    print_crash_location(signal == SIGABRT ? "abort (failed assert?)" : "signal");
    std::_Exit(1);
}

#ifdef _WIN32
// Not called with a debugger attached, which stops at the crash instead.
// __fastfail (used by STL hardening checks) bypasses it.
static LONG WINAPI report_crash(EXCEPTION_POINTERS* info) {
    char what[64];
    std::snprintf(what, sizeof(what), "exception 0x%08lX", info->ExceptionRecord->ExceptionCode);
    print_crash_location(what);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void install_crash_handlers() {
    std::signal(SIGABRT, on_crash_signal);
#ifdef _WIN32
    SetUnhandledExceptionFilter(report_crash);
#else
    std::signal(SIGSEGV, on_crash_signal);
    std::signal(SIGBUS, on_crash_signal);
    std::signal(SIGFPE, on_crash_signal);
    std::signal(SIGILL, on_crash_signal);
#endif
}

static void print_usage() {
#ifdef _WIN32
    std::printf("usage: test.bat [-v | --verbose] [--seed N] [name filter...]\n");
#else
    std::printf("usage: ./test.sh [-v | --verbose] [--seed N] [name filter...]\n");
#endif
}

static bool matches(const Test& test, const char* const* filters, i32 filter_count) {
    if (filter_count == 0) {
        return true;
    }
    for (i32 i = 0; i < filter_count; ++i) {
        if (std::strstr(test.name, filters[i])) {
            return true;
        }
    }
    return false;
}

static double seconds_now() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    TestState& state = g_test_state;

    bool verbose = false;
    constexpr i32 k_max_filters = 64;
    const char* filters[k_max_filters];
    i32 filter_count = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(arg, "--seed") == 0) {
            char* end = nullptr;
            if (i + 1 < argc) {
                state.seed = std::strtoull(argv[++i], &end, 10);
            }
            if (!end || *end != 0 || end == argv[i]) {
                std::printf("--seed needs a number\n");
                print_usage();
                return 2;
            }
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (arg[0] == '-') {
            std::printf("unknown option %s\n", arg);
            print_usage();
            return 2;
        } else if (filter_count < k_max_filters) {
            filters[filter_count++] = arg;
        } else {
            std::printf("too many name filters (max %d)\n", k_max_filters);
            return 2;
        }
    }

    if (state.too_many_tests) {
        std::printf("error: more than %d tests; raise k_max_tests in tests/test.h\n", k_max_tests);
        return 2;
    }

    install_crash_handlers();

    static const Test* failed_tests[k_max_tests];
    i32 run_count = 0;
    i32 failed_count = 0;
    i32 total_checks = 0;
    i32 total_failed_checks = 0;
    const double run_start = seconds_now();

    for (i32 i = 0; i < state.test_count; ++i) {
        const Test& test = state.tests[i];
        if (!matches(test, filters, filter_count)) {
            continue;
        }

        state.current = &test;
        state.checks = 0;
        state.failed_checks = 0;
        state.rng_state = test_seed_for(test, state.seed);
        state.rng_used = false;

        const double start = seconds_now();
        test.fn();
        const double ms = (seconds_now() - start) * 1000.0;

        ++run_count;
        total_checks += state.checks;
        total_failed_checks += state.failed_checks;
        if (state.failed_checks > 0) {
            failed_tests[failed_count++] = &test;
            std::printf("     %d of %d checks failed\n", state.failed_checks, state.checks);
            if (state.rng_used && state.seed != 0) {
                std::printf(
                    "     uses random inputs: rerun with  test.bat %s --seed %llu\n",
                    test.name,
                    static_cast<unsigned long long>(state.seed)
                );
            } else if (state.rng_used) {
                std::printf("     uses random inputs: rerun with  test.bat %s  (same values every run)\n", test.name);
            }
        } else if (verbose) {
            std::printf(
                "PASS %s (%d check%s, %.2f ms)%s\n",
                test.name,
                state.checks,
                state.checks == 1 ? "" : "s",
                ms,
                state.checks == 0 ? "  <- no checks ran" : ""
            );
        }
        // Keep output in order when piped (e.g. into neovim), and keep it if a later test crashes.
        std::fflush(stdout);
    }
    state.current = nullptr;
    const double total_ms = (seconds_now() - run_start) * 1000.0;

    if (run_count == 0) {
        std::printf("no tests match");
        for (i32 i = 0; i < filter_count; ++i) {
            std::printf(" \"%s\"", filters[i]);
        }
        std::printf(" (%d tests exist; -v lists them)\n", state.test_count);
        return 1;
    }

    if (failed_count == 0) {
        std::printf("all %d tests passed (%d checks, %.1f ms)\n", run_count, total_checks, total_ms);
        return 0;
    }

    std::printf(
        "\n%d of %d tests failed (%d of %d checks):\n",
        failed_count,
        run_count,
        total_failed_checks,
        total_checks
    );
    for (i32 i = 0; i < failed_count; ++i) {
        std::printf("%s:%d: %s\n", failed_tests[i]->file, failed_tests[i]->line, failed_tests[i]->name);
    }
    return 1;
}
