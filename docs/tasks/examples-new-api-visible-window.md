# Horizon Examples New API Visible Window Notes
<!-- TASK_DOCS_EXAMPLES_NEW_API_VISIBLE_WINDOW_ZH_CN_SHA256: 2f6ab4c6ad11e8a25a384d989b1e46a35e057eb5668e7875d2904d61a9a12c61 -->

## When To Use

- Use this when restoring or validating `HorizonExamples` with the new public API: open a GLFW window, submit Vulkan rendering, and present.
- Stay on the refactored new API path. Do not restore old public names such as `ImageFormat`, `BufferUsage`, or `HardwareImageCreateInfo` just to run examples.
- `examples/example_baseline`, `example_default`, `example_edsl`, and `example_glsl` should all be compiled into `HorizonExamples`; if APIs break, migrate call sites to the current API instead of restoring old public compatibility names.
- Current first acceptance is real visible-window smoke for the explicit `baseline`, `default`, `edsl`, and `glsl` entries. `default` should also cover the combined EDSL/GLSL windows. Restore texture, compute, multi-window, and stress as later modes if needed.

## Entrypoints And Scope

- The public umbrella is `include/horizon.h`; old `include/Horizon.h` and `horizon_refac.h` names are migration history and should not be reintroduced in new examples.
- The executable entrypoint is `examples/main.cpp`; it defaults to `default` and currently supports `baseline`, `default`, `edsl`, and `glsl`.
- `examples/CMakeLists.txt` should compile `common.cpp`, `example_baseline`, `example_default`, `example_edsl`, and `example_glsl`, and copy baseline precompiled SPIR-V files beside the target output.
- Do not assume `--frames`, `--threads`, `texture`, `compute`, `multi-window`, or `stress` exists. Check the current `examples/main.cpp` CLI before adding validation commands.
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

## Runtime Bug Triage

- Do not treat a still-running process as a successful smoke. MSVC Debug CRT assertions leave the process alive behind a `Microsoft Visual C++ Runtime Library` window; smoke scripts should enumerate visible process window titles and fail on that title.
- Launch examples once from the repo root and once from the target output directory. Old baseline code may read `readFile("shaders/xxx.spv")` through a relative path; if only the output directory works, first check shader post-build copy, working directory, and source-directory fallback.
- Before screenshot validation, bring the target window to the foreground/topmost and wait at least one or two frames. Record window title, exit code, stderr, and screenshot hash; if different modes produce identical screenshot hashes, or the title does not match the mode, that screenshot batch is not valid evidence that rendering is correct.
- Resize / minimize / hide validation should cover `default`, `edsl`, and `glsl`, and should exercise both the EDSL and GLSL windows in `default`. Success means no early exit, empty stderr or no bad stderr patterns, no CRT assertion window, clean `WM_CLOSE`, and no leftover `HorizonExamples` process.
- For `vector subscript out of range`, inspect CPU containers and loop bounds before deep Vulkan debugging: cube constants should have 36 vertices; aggregate initializer changes must not drift `vertices.size()`; draw loops should use stable object counts, not containers being changed by another thread.
- In threaded default paths, first look for shared `std::vector` `push_back` / `resize` / read races. Per-window/per-object storage-buffer containers should be created to fixed size before threads start; threads should only update existing buffer contents.
- If threaded `default` render and display paths share one output image, display present should explicitly wait for the latest render submission. Otherwise resize/minimize/hide can make image-layout, queue-submit, or binary-semaphore reuse bugs look like ordinary window bugs.
- For `HardwareImage` / `ResourceStore` / `HorizonExamples.exe default` crashes, validation errors, or sync errors that reproduce only on AMD integrated GPUs or specific drivers, debug backend image layout, barriers, swapchain image/semaphore reuse, present-completion points, and queue serialization first. Unless the user explicitly asks for a shader-source fix, do not edit `examples/shaders/*.glsl` or use shader changes to mask backend issues.
- For handwritten GLSL or EDSL through the current pipeline API, if `bindless space index unavailable` appears or assertions happen near `ComputePipelineDesc::from_source(...)` / `RasterizerPipelineDesc::from_source(...)`, do not use disabled bindless as a long-term workaround. First inspect whether shader-reflected set/binding metadata matches the backend bindless table ABI; UBOs and ordinary descriptors should be generated from reflected set/binding values.
- For this task's `default` repro, capture the stack first, then set `HORIZON_VULKAN_DIAGNOSTICS_PATH`, launch `HorizonExamples.exe default` from `build\ninja-msvc\examples\Debug`, wait 10-20 seconds, close or terminate the process, then filter `horizon-vulkan-diagnostics.txt` for `warning[E39012]`, `ComputePipeline reflection`, `VUID`, `SYNC-HAZARD`, and `VK_ERROR`.
- For default bindless failures after the Helicon Slang migration, separate reflection/layout errors from submit/layout synchronization errors. If diagnostics show `inputImageRGBA16 type=_Texture set=2 binding=0 bindType=8 elementCount=0`, and `VUID-VkComputePipelineCreateInfo-layout-07988` / missing `pSetLayouts[2]` no longer appears, storage-image bindless reflection and pipeline layout are basically aligned. Do not keep adding `set` / `binding` to `BindingKey`, `BoundField`, or `AutoBindEntry`; do not restore `BindlessSpaceIndex`; do not treat disabled bindless as the final fix.
- If the next errors become `VUID-vkCmdDraw-None-09600` and the object name is `example_default.output`, first inspect whether `VkDescriptorImageInfo::imageLayout` at descriptor write time matches the image's real layout when commands execute. Focus on `src/hardware_wrapper_vulkan/hardware/resource_manager.cpp` `store_sampled_descriptor()` / `store_storage_descriptor()`, transient descriptor writes in `src/hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.cpp` and `vulkan_rasterizer_pipeline.cpp`, and `Dispatch`, `BeginRendering`, `DrawIndexed`, and `Present` image transitions in `src/hardware_wrapper_vulkan/hardware/execution.cpp`.
- If diagnostics also report `SYNC-HAZARD-WRITE-RACING-READ` or `SYNC-HAZARD-WRITE-RACING-WRITE` involving `vkCmdBlitImage` and another command buffer's `vkCmdPipelineBarrier2`, the likely root cause is not missing Slang binding fields. It is that render/display submits on different `VkQueue`s leave the same output image's layout/read/write accesses without same-queue ordering or an explicit GPU dependency. Prefer executor/backend queue selection, resource dependency, or receipt-passing fixes; do not change the example so render waits for display completion, and do not add three-image frame-slot scheduling for `default`.
- The intended default render/display split is: render produces one offscreen output image; display waits for the latest render receipt before present, consumes that image, and copies/blits it to the swapchain. Render should not wait for a display receipt just to reuse `finalOutputImages[i]`; keep the example thread-split, one-output-image shape, and DLSS-style insertion point, while ensuring the output image format / usage fits the render target, storage/compute, and display copy/present paths.
- After a fix, smoke all explicit entries: `baseline`, `default`, `edsl`, and `glsl`. For `default`, confirm both EDSL and GLSL windows exist and no CRT assertion window appears.

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
build\ninja-msvc\examples\Debug\HorizonExamples.exe baseline
build\ninja-msvc\examples\Debug\HorizonExamples.exe default
build\ninja-msvc\examples\Debug\HorizonExamples.exe edsl
build\ninja-msvc\examples\Debug\HorizonExamples.exe glsl

git diff --check
```

`HorizonTests` should have a reasonable CTest `TIMEOUT`; real Vulkan tests should skip or return failure when Vulkan is unavailable, not hang indefinitely.
