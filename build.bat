@echo off

if not exist .\build mkdir .\build
pushd .\build
clang-cl -DDEV -FC -Zi ..\src\win32.cpp user32.lib gdi32.lib
popd
