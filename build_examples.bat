@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d e:\Horizon\build\conan\examples\release
cmake --build . --config Release --target HorizonExamples
