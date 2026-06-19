@echo off
cmake --preset windows-msvc || exit /b 1
cmake --build --preset windows-release
