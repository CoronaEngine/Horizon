# Horizon Examples New API Visible Window Notes
<!-- TASK_DOCS_EXAMPLES_NEW_API_VISIBLE_WINDOW_ZH_CN_SHA256: 00ec90155cd69a1a748e106b1ac94dceeb35899e1042009a03bc8f6e4b152c99 -->

## When To Use

- Use this when restoring or validating `HorizonExamples` with the new public API: open a GLFW window, submit Vulkan rendering, and present.
- Stay on the refactored new API path. Do not restore old public names such as `ImageFormat`, `BufferUsage`, or `HardwareImageCreateInfo` just to run examples.
- Old `examples/example_baseline`, `example_glsl`, and `example_edsl` may remain references, but should not be compiled for the first-stage acceptance by default.
- First-stage acceptance is only the visible-window and present smoke path; texture, compute, EDSL, GLSL, and old multi-window default behavior remain later migration batches.

## Entrypoints And Scope

- The public umbrella is `include/Horizon.h`; it should export `format.h`, `resource.h`, `horizon_refac.h`, and the new execution/display facades.
- The executable entrypoint is `examples/main.cpp`; it defaults to `default` and should support `--frames N` for automatic exit.
- The current runnable sample lives in `examples/example_default/`: one window, one offscreen color target, and a minimal primitive or fullscreen triangle.
- The second-stage three-thread path is enabled explicitly with `--threads mesh-render-display`; the default keeps the first-stage single-thread smoke path.
- Keep implementation work in `src/hardware_wrapper` and `src/hardware_wrapper_vulkan`; treat `src/HardwareWrapper` and `src/HardwareWrapperVulkan` as historical references.

## Three-Thread Second Stage

- The `mesh` thread owns CPU mesh generation or updates and produces immutable frame snapshots or upload requests; it must not share mutable mesh containers.
- The `render` thread consumes mesh snapshots and submits `executor.stream() << ... << present(displayer, image) << commit()`.
- The `display` thread owns the GLFW window lifecycle, event polling, exit signal, and `HardwareDisplayer` creation; the main thread only starts, joins, and aggregates errors.
- `present(displayer, image)` remains an execution graph node; do not move present into a post-commit side step on the display thread.
- `HardwareBuffer`, `HardwareImage`, and pipeline wrappers may cross threads by copied handles only; do not mutate backend state through raw cross-thread access.

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

git diff --check
```

`HorizonTests` should have a reasonable CTest `TIMEOUT`; real Vulkan tests should skip or return failure when Vulkan is unavailable, not hang indefinitely.
