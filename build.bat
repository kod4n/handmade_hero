@echo off
setlocal

if not exist .\build mkdir .\build
pushd .\build

set CXX=clang-cl
REM set CXX_FLAGS=/std:c++20 -DDEV -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-c++98-compat -Wno-pre-c++20-compat-pedantic -FC -Zi
REM missing-designated-field-initializers: Win32 structs have many fields that are meant to stay zero
REM _MSVC_STL_HARDENING_SPAN: bounds-check std::span indexing/subspan in dev builds
set CXX_FLAGS=/std:c++20 -DDEV -D_MSVC_STL_HARDENING_SPAN=1 -W4 -Werror -GR- -EHa- -Oi -Wno-missing-designated-field-initializers -FC -Zi
set LIBS=user32.lib gdi32.lib
for %%I in (..\src\win32.cpp) do set SOURCE=%%~fI

%CXX% %CXX_FLAGS% "%SOURCE%" %LIBS%

if errorlevel 1 (
    popd
    exit /b 1
)

REM Generate compile_commands.json for clangd.
REM Link inputs (.lib) are left out on purpose: clangd drops /std:c++20 when
REM the command has extra inputs, and parses the file as C++14 instead.
REM "command" (one string) is used instead of "arguments" so flags containing
REM = or , (e.g. -DFOO=1) don't get split by the batch for loop.
(
    echo [
    echo   {
    echo     "directory": "%CD:\=/%",
    echo     "command": "%CXX% %CXX_FLAGS% -c %SOURCE:\=/%",
    echo     "file": "%SOURCE:\=/%"
    echo   }
    echo ]
) > ..\compile_commands.json

popd
endlocal
