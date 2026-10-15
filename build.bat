@echo off
rem Bootstrap wrapper for the windows-msvc preset -- NOT an alternative build system.
rem CMakePresets.json hard-requires $env{VCPKG_ROOT} in its toolchainFile, and MSVC needs
rem a vcvars64 environment. On a fresh VS-only box neither is set, so `cmake --preset
rem windows-msvc` fails before it starts. This script discovers both via vswhere (VS's
rem bundled vcpkg + its bundled cmake + vcvars64), points the vcpkg download/binary caches
rem at build/, and then runs exactly the documented preset commands. Once VCPKG_ROOT is
rem set in your environment, invoke the presets directly and skip this.
setlocal
pushd "%~dp0" || exit /b 1

set "REPO_ROOT=%CD%"

set "CMAKE_EXE=cmake"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
    set "VSINSTALLDIR=%%I"
  )
)

if defined VSINSTALLDIR (
  if not defined VCPKG_ROOT (
    if exist "%VSINSTALLDIR%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
      set "VCPKG_ROOT=%VSINSTALLDIR%\VC\vcpkg"
    )
  )
  if exist "%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_EXE=%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  )
  if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)

if /I "%CMAKE_EXE%"=="cmake" (
  where cmake >nul 2>nul || (
    echo CMake was not found on PATH and no Visual Studio CMake install was found.
    exit /b 1
  )
) else (
  if not exist "%CMAKE_EXE%" (
    echo CMake was not found at "%CMAKE_EXE%".
    exit /b 1
  )
)

if not defined VCPKG_ROOT (
  echo VCPKG_ROOT is not set and no Visual Studio vcpkg install was found.
  exit /b 1
)

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo vcpkg toolchain file was not found under VCPKG_ROOT="%VCPKG_ROOT%".
  exit /b 1
)

if not defined VCPKG_DOWNLOADS (
  set "VCPKG_DOWNLOADS=%REPO_ROOT%\build\vcpkg-downloads"
)

if not defined VCPKG_DEFAULT_BINARY_CACHE (
  set "VCPKG_DEFAULT_BINARY_CACHE=%REPO_ROOT%\build\vcpkg-bincache"
)

if not exist "%VCPKG_DOWNLOADS%" (
  mkdir "%VCPKG_DOWNLOADS%" || exit /b 1
)

if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" (
  mkdir "%VCPKG_DEFAULT_BINARY_CACHE%" || exit /b 1
)

"%CMAKE_EXE%" --preset windows-msvc || exit /b 1
"%CMAKE_EXE%" --build --preset windows-release || exit /b 1
