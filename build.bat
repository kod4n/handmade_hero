@echo off
setlocal

if not exist .\build mkdir .\build
pushd .\build

set CXX=clang-cl
REM set CXX_FLAGS=/std:c++20 -DDEV -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-c++98-compat -Wno-pre-c++20-compat-pedantic -FC -Zi
set CXX_FLAGS=/std:c++20 -DDEV -W4 -FC -Zi
for %%I in (..\src\win32.cpp) do set SOURCE=%%~fI

%CXX% %CXX_STANDARD% %CXX_FLAGS% "%SOURCE%" user32.lib gdi32.lib

if errorlevel 1 (
    popd
    exit /b 1
)

REM Generate compile_commands.json
(
    echo [
    echo   {
    echo     "directory": "%CD:\=/%",
    echo     "arguments": [
    echo       "%CXX%",
    for %%F in (%CXX_FLAGS%) do echo       "%%F",
    echo       "%SOURCE:\=/%",
    echo       "user32.lib",
    echo       "gdi32.lib"
    echo     ],
    echo     "file": "%SOURCE:\=/%"
    echo   }
    echo ]
) > ..\compile_commands.json

popd
endlocal
