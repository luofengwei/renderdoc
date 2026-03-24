@echo off
REM host_cpp.bat — Wrapper to make cl.exe accept "c++ main.cpp -o output" syntax
REM Used by RenderDoc's CMake for cross-compiling include-bin tool

setlocal enabledelayedexpansion

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set SRC=
set OUT=

:parse
if "%~1"=="" goto build
if "%~1"=="-o" (
    set OUT=%~2
    shift
    shift
    goto parse
)
set SRC=%~1
shift
goto parse

:build
if "%OUT%"=="" (
    echo ERROR: No output specified
    exit /b 1
)
cl.exe /nologo /EHsc /Fe:"%OUT%.exe" "%SRC%"
exit /b %errorlevel%
