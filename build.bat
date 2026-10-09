@echo off

if not exist .\build mkdir .\build
pushd .\build
clang-cl -FC -Zi ..\src\*.cpp user32.lib gdi32.lib
popd
