@echo off
rem Windows build: Visual Studio (cl, run from a Developer Command Prompt) or MinGW (g++).
rem Binaries go to bin\. Stack size is raised to 256 MB (deep recursion in ida).
cd /d "%~dp0"
if not exist bin mkdir bin
set SRC=src\common.cpp src\io.cpp src\route.cpp src\exact.cpp src\reduce.cpp src\pdb.cpp src\ida.cpp src\greedy.cpp src\beam.cpp src\best.cpp
where cl >nul 2>nul
if %errorlevel%==0 (
  for %%a in (exact ida reduce greedy beam best) do (
    cl /nologo /O2 /EHsc /std:c++17 /utf-8 src\main_%%a.cpp %SRC% /Fe:bin\p15_%%a.exe /Fo:bin\ /link /STACK:268435456 || exit /b 1
  )
  del bin\*.obj 2>nul
) else (
  for %%a in (exact ida reduce greedy beam best) do (
    g++ -O2 -std=c++17 src\main_%%a.cpp %SRC% -o bin\p15_%%a.exe -Wl,--stack,268435456 || exit /b 1
  )
)
echo done
