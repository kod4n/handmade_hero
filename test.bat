@echo off
setlocal

REM Builds and runs the tests. Arguments go to the runner (see tests\main.cpp):
REM   test.bat                 run every test; print failures and a summary
REM   test.bat -v              also list every test with its check count and time
REM   test.bat tile bmp        run only tests whose name contains "tile" or "bmp"
REM   test.bat --seed 42       give the random inputs different values

REM build.bat works relative to the current directory, so run from the repo root.
pushd "%~dp0"
CALL .\build.bat test > NUL || goto :build_failed
.\build\tests.exe %*
set TEST_RESULT=%ERRORLEVEL%
popd
exit /b %TEST_RESULT%

:build_failed
REM Compiler errors went to stderr and are already on screen.
popd
echo test build failed
exit /b 1
