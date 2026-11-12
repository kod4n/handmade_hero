@echo off
setlocal

if not exist .\build mkdir .\build
pushd .\build

set CXX=clang-cl
REM set CXX_FLAGS=/std:c++20 -DDEV -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-c++98-compat -Wno-pre-c++20-compat-pedantic -FC -Zi
REM missing-designated-field-initializers: Win32 structs have many fields that are meant to stay zero
REM _MSVC_STL_HARDENING_SPAN: bounds-check std::span indexing/subspan in dev builds
set CXX_FLAGS=/std:c++20 -DDEV -D_MSVC_STL_HARDENING_SPAN=1 -W4 -Werror -GR- -EHa- -Oi -Wno-missing-designated-field-initializers -FC -Zi
set LIBS=user32.lib gdi32.lib winmm.lib ole32.lib
for %%I in (..\src\win32.cpp) do set SOURCE=%%~fI
for %%I in (..\src\game.cpp) do set GAME_SOURCE=%%~fI

REM ---- Game DLL (hot reloadable) ----
REM lock.tmp tells the running exe not to load game.dll while it's being written.
REM The PDB gets a random name each build: a debugger attached to the running
REM exe keeps the previous PDB locked, which would otherwise make the link fail.
echo building > lock.tmp
del game_*.pdb > NUL 2> NUL
%CXX% %CXX_FLAGS% "%GAME_SOURCE%" -LD -Fegame.dll /link -incremental:no -PDB:game_%RANDOM%.pdb -EXPORT:game_render -EXPORT:game_sound
set GAME_BUILD_ERROR=%ERRORLEVEL%
del lock.tmp
if not %GAME_BUILD_ERROR%==0 (
    popd
    exit /b 1
)

REM ---- Platform exe ----
REM The exe is locked while it runs, so skip it; the game DLL is what reloads.
tasklist /FI "IMAGENAME eq win32.exe" /NH | find /I "win32.exe" > NUL
if not errorlevel 1 (
    echo win32.exe is running, skipping platform build
    goto :compile_commands
)

%CXX% %CXX_FLAGS% "%SOURCE%" %LIBS%
if errorlevel 1 (
    popd
    exit /b 1
)

:compile_commands
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
    echo   },
    echo   {
    echo     "directory": "%CD:\=/%",
    echo     "command": "%CXX% %CXX_FLAGS% -c %GAME_SOURCE:\=/%",
    echo     "file": "%GAME_SOURCE:\=/%"
    echo   }
    echo ]
) > ..\compile_commands.json

popd
endlocal
