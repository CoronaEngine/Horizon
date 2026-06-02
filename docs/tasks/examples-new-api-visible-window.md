# Horizon Examples New API Visible Window Notes
<!-- TASK_DOCS_EXAMPLES_NEW_API_VISIBLE_WINDOW_ZH_CN_SHA256: 80a1c34bd41cb9d0adc582b096be8f122d5cc79147297a764f90714060b2e1a4 -->

## When To Use

- Use this when restoring or validating `HorizonExamples` with the new public API: open a GLFW window, submit Vulkan rendering, and present.
- Stay on the refactored new API path. Do not restore old public names such as `ImageFormat`, `BufferUsage`, or `HardwareImageCreateInfo` just to run examples.
- Old `examples/example_baseline`, `example_glsl`, and `example_edsl` may remain references; current runnable samples migrate into new API modes under `examples/example_default/` instead of recompiling the old example targets.
- First-stage acceptance is the visible-window and present smoke path; second stage is the `mesh/render/display` threaded default smoke; the third migration batch restores minimal texture, compute, EDSL, GLSL, and multi-window smoke modes; use the separate `stress` mode for multi-window / multi-render-thread validation.

## Entrypoints And Scope

- The public umbrella is `include/Horizon.h`; it should export `format.h`, `resource.h`, `horizon_refac.h`, and the new execution/display facades.
- The executable entrypoint is `examples/main.cpp`; it defaults to `default` and should support `--frames N` for automatic exit.
- The current runnable samples live in `examples/example_default/` and cover new API smoke modes: `default`, `glsl`, `edsl`, `texture`, `compute`, and `multi-window`; `stress` mode is for concurrent multi-window present pressure.
- The second-stage three-thread path is enabled explicitly with `default --threads mesh-render-display`; `--threads` applies only to default mode, while the other modes remain single-thread smoke paths.
- Keep implementation work in `src/hardware_wrapper` and `src/hardware_wrapper_vulkan`; treat `src/HardwareWrapper` and `src/HardwareWrapperVulkan` as historical references.

## Three-Thread Second Stage

- The second stage is still limited to one window, one render target, and a minimal mesh; texture, compute, EDSL, GLSL, and multi-window migrate as separate smoke modes instead of being folded into the threaded default pipeline.
- The `mesh` thread owns CPU mesh generation or updates and produces immutable frame snapshots or upload requests; it must not share mutable mesh containers.
- The `render` thread consumes mesh snapshots and submits `executor.stream() << ... << present(displayer, image) << commit()`.
- The `display` thread owns the GLFW window lifecycle, event polling, exit signal, and `HardwareDisplayer` creation; the main thread only starts, joins, and aggregates errors.
- `present(displayer, image)` remains an execution graph node; do not move present into a post-commit side step on the display thread.
- `HardwareBuffer`, `HardwareImage`, and pipeline wrappers may cross threads by copied handles only; do not mutate backend state through raw cross-thread access.

## Third-Batch Smoke Modes

- `glsl` uses an inline GLSL fullscreen triangle to validate GLSL compilation through the new `RasterizerPipeline` and present path.
- `edsl` uses a minimal EDSL indexed triangle to validate EDSL codegen, reflection, and the new rasterizer facade.
- `texture` uploads CPU checker data into an image and presents it, validating buffer-to-image copy, image layout transition, and present.
- `compute` uses a minimal GLSL compute shader to write a storage image and present it, validating `ComputePipeline::command_batch()`, storage image descriptors, and the dispatch encoder.
- `multi-window` uses two GLFW Win32 windows and two render targets to validate that the lower-case display/swapchain path can create and present multiple windows.
- These modes are still smoke tests. Do not restore the old default example's combined texture, compute, EDSL, GLSL, or multi-window behavior, and do not restore the old public API compatibility layer.

## Multi-Window Stress Mode

- `stress` mode validates concurrent paths with multiple GLFW windows, multiple mesh threads, multiple render threads, and multiple swapchain presents. It defaults to 8 windows and 4 render threads; tune it with `--windows N` and `--render-threads N`.
- Each window should be presented by only one render thread to avoid multiple threads touching the same swapchain; multiple windows may still share the main-device present queue, which exercises `Queue` serialization for submit / present host access.
- `stress` is still a single-render-target, minimal dynamic-mesh pressure smoke. It is not a restoration of the old default example's combined multi-window behavior.

## Present Path

- `present(displayer, image)` is an execution graph command node, not a side step after `commit()`.
- The lower-case `DisplayManager` owns Win32 surface creation, swapchain acquire, waiting for the render submit token, copying/blitting the offscreen image to the swapchain image, signaling the binary semaphore, and `vkQueuePresentKHR`.
- First stage only requires main-device present. If the main-device queue does not support surface present, return a clear error instead of silently taking cross-device or CPU bridge paths.
- Swapchain `OUT_OF_DATE`, `SUBOPTIMAL`, skipped, and presented states should flow through `SubmitReceipt` / present results.

## Lifetime Pitfalls

- When `RasterizerPipeline` records a draw, internal `RecordedDraw` must not strongly hold its own public `ResourceHandle`; that creates a cycle: pipeline token -> impl -> draw -> pipeline token.
- Store a weak token for the pipeline and lock it only while building `command_batch()` to fill `DrawIndexedDesc::pipeline`.
- If VMA reports `Some allocations were not freed` at test end, first inspect whether pipeline, command batch, or queue in-flight keep-alives still strongly retain buffer/image tokens.
- After changing private class layout in public headers, if MSVC/Ninja incremental builds crash in impossible ways, run a clean rebuild before debugging source-level ownership.

## Validation

```powershell
cmake --preset ninja-msvc -DHORIZON_BUILD_EXAMPLES=ON -DHORIZON_BUILD_TESTS=ON
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target HorizonExamples
cmake --build --preset msvc-debug --target HorizonTests

build\ninja-msvc\tests\Debug\HorizonTests.exe hardware_context.lazy_construction
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.stream_facade_commit
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.rasterizer_pipeline_ir
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.present_receipt
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.mesh_render_display_threads
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.rasterizer_pipeline_real_vulkan_render
build\ninja-msvc\examples\Debug\HorizonExamples.exe --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe default --threads mesh-render-display --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe glsl --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe edsl --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe texture --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe compute --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe multi-window --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe stress --windows 8 --render-threads 4 --frames 20
build\ninja-msvc\examples\Debug\HorizonExamples.exe stress --windows 16 --render-threads 16 --frames 120

git diff --check
```

`HorizonTests` should have a reasonable CTest `TIMEOUT`; real Vulkan tests should skip or return failure when Vulkan is unavailable, not hang indefinitely.
