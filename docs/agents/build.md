# Horizon Build Context
<!-- AGENT_DOCS_BUILD_ZH_CN_SHA256: 2bf57c472e676d7ff781565fdd93acd94f19380c04291bcc8343b1933983d44e -->

## Unified entry point

Developer machines need uv, CMake, Ninja, and the Visual Studio C++ toolchain. uv installs the locked Conan version; a system Conan installation is not required.

```text
uv run --frozen python tools/dev.py status
uv run --frozen python tools/dev.py configure
uv run --frozen python tools/dev.py build
uv run --frozen python tools/dev.py build ShaderCompileScripts
uv run --frozen python tools/dev.py build HorizonExamples
```

The default configuration is `RelWithDebInfo` and the default target is `HorizonExamples`. Select another configuration with `--configuration Debug|Release|RelWithDebInfo|MinSizeRel`.

`build` installs dependencies, configures, and compiles. `build-fast` reuses an existing build tree. `rebuild` removes only the selected configuration directory before rebuilding. `clean` removes generated project directories.

## IDEs

Visual Studio, VSCode CMake Tools, and CLion should select a stable preset from the root `CMakePresets.json`. The first configure invokes the uv/Python bootstrap before `project()` and generates the Conan toolchain under `build/conan/<configuration>`.

## Conan boundary

`conanfile.py` is a development dependency and toolchain manifest, not a Horizon package recipe. Do not use `conan create` or add package, editable, or source-export workflows.

Target names enable development options as needed:

- `HorizonExamples` enables examples.
- `ShaderCompileScripts` enables tools.
- `HorizonTests` enables tests.
- `HorizonSmokeBenchmarks` enables benchmarks.
- `ocarina*` and `vision-hotfix*` targets enable CUDA/Ocarina as required and therefore require `CUDA_PATH`.

Slang and the other local recipes are exported before install/configure/build. CMake has no `HORIZON_SLANG_ROOT` or `third-party/slang` fallback.

## Validation

```text
uv run --frozen python -m unittest tools/test_workflow.py
uv run --frozen python tools/dev.py configure
uv run --frozen python tools/dev.py build-fast HorizonExamples
```

Call Conan or CMake directly only while diagnosing the lower-level workflow.
