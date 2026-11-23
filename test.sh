#!/bin/sh
# Builds and runs the unit tests in src/tests.cpp on macOS and Linux.
# Exits non-zero if the build or any check fails. On Windows, use test.bat.
#
#   ./test.sh      (or: sh test.sh)
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SRC_DIR="$ROOT_DIR/src"

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
# it to compile_commands.json so clangd knows how to parse tests.cpp.
# shellcheck disable=SC2086
"$CXX" $CXX_FLAGS -MJ tests.json "$SRC_DIR/tests.cpp" -o tests

./tests
