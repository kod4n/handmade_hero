@echo off

if not exist .\build mkdir .\build
pushd .\build
clang-cl -Zi ..\src\*.cpp user32.lib
popd
