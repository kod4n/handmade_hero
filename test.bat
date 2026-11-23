@echo off
setlocal

REM Builds and runs the unit tests in src\tests.cpp.
REM Exits non-zero if the build or any check fails.

if not exist .\build mkdir .\build
pushd .\build

set CXX=clang-cl
REM Same flags as build.bat, so the game code is tested as the game builds it.
set CXX_FLAGS=/std:c++20 -DDEV -D_MSVC_STL_HARDENING_SPAN=1 -W4 -Werror -Wimplicit-fallthrough -GR- -EHa- -Oi -Wno-missing-designated-field-initializers -FC -Zi
for %%I in (..\src\tests.cpp) do set TEST_SOURCE=%%~fI

%CXX% %CXX_FLAGS% "%TEST_SOURCE%" -Fetests.exe
if errorlevel 1 (
    popd
    exit /b 1
)

.\tests.exe
set TEST_RESULT=%ERRORLEVEL%
popd
exit /b %TEST_RESULT%
