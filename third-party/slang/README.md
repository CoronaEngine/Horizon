# Slang binary package

This directory is intentionally kept almost empty in git.

`cmake/HorizonSlang.cmake` downloads the pinned Slang release archive during
CMake configure, caches it under `third-party/slang/download/`, and extracts the
usable distribution to `third-party/slang/src/`.

The generated directories are ignored so large Slang DLL/EXE/LIB files do not
get uploaded with normal source changes.
