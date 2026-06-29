# Slang binary package

This directory is intentionally kept almost empty in git.

`cmake/horizon_slang.cmake` consumes a prepared Slang SDK or pinned archive.
CMake configure no longer downloads Slang; pre-populate
`third-party/slang/download/` or point `HORIZON_SLANG_ROOT` at an extracted SDK.
The usable distribution is expected under `third-party/slang/src/` for the
default local path.

The generated directories are ignored so large Slang DLL/EXE/LIB files do not
get uploaded with normal source changes.
