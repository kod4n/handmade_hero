#!/bin/sh
# Builds and runs the tests on macOS and Linux. On Windows, use test.bat.
# Arguments go to the runner (see tests/main.cpp):
#   ./test.sh                run every test; print failures and a summary
#   ./test.sh -v             also list every test with its check count and time
#   ./test.sh tile bmp       run only tests whose name contains "tile" or "bmp"
#   ./test.sh --seed 42      give the random inputs different values
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SRC_DIR="$ROOT_DIR/src"
TEST_SOURCE="$ROOT_DIR/tests/main.cpp"

CXX="${CXX:-clang++}"

# Same flags as mac_build / linux_build, so the game code is tested as the
# game builds it. Only the span bounds-checking macro differs: libc++ on
# macOS, libstdc++ on Linux.
case "$(uname -s)" in
Darwin) HARDENING="-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG" ;;
*) HARDENING="-D_GLIBCXX_ASSERTIONS" ;;
esac
CXX_FLAGS="-std=c++20 -DDEV $HARDENING \
-g -O0 -Wall -Wextra -Werror -Wimplicit-fallthrough -fno-rtti -fno-exceptions \
-Wno-missing-designated-field-initializers"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# -MJ writes a compile_commands.json fragment; mac_build and linux_build add
# it to compile_commands.json so clangd knows how to parse the tests.
# tests/main.cpp includes every test file; -I lets them include game headers
# by name.
# shellcheck disable=SC2086
"$CXX" $CXX_FLAGS -I"$SRC_DIR" -MJ tests.json "$TEST_SOURCE" -o tests

./tests "$@"
