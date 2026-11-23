@echo off
setlocal

echo ======================================================================
echo Building Cost-Aware Adaptive Work-Stealing Scheduler
echo ======================================================================

set COMPILER=d:\mingw64\bin\g++.exe
if not exist "%COMPILER%" (
    where g++.exe >nul 2>nul
    if %errorlevel% equ 0 (
        set COMPILER=g++.exe
    ) else (
        echo ERROR: g++.exe not found!
        exit /b 1
    )
)

if not exist "d:\MAP_PROJECT\build" mkdir "d:\MAP_PROJECT\build"

echo Using compiler: %COMPILER%
"%COMPILER%" -std=c++17 -O3 -Wall -Wextra ^
    -static -static-libgcc -static-libstdc++ ^
    -I d:\MAP_PROJECT\cpp\include ^
    -I d:\MAP_PROJECT\cpp\vendor ^
    d:\MAP_PROJECT\cpp\src\*.cpp ^
    d:\MAP_PROJECT\cpp\main.cpp ^
    -o d:\MAP_PROJECT\build\scheduler.exe

if %errorlevel% equ 0 (
    echo [SUCCESS] Engine compiled to d:\MAP_PROJECT\build\scheduler.exe
) else (
    echo [FAILURE] Compilation failed with error code %errorlevel%
    exit /b %errorlevel%
)
