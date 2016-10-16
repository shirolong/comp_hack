@echo off
mkdir build32 > nul 2> nul
cd build32
set CMAKE_PREFIX_PATH=C:\Qt\Qt5.7.0\5.7\msvc2015_32
cmake -G"Visual Studio 14 2015" ..
pause
