# Hardware image readback

`HardwareImage::readback(executor, layer, mip)` synchronously copies a submitted
image subresource into an owning `ImageReadback`. The result contains the actual
subresource extent, `Format`, byte row/slice pitches, and pixel storage. It does
not flip rows, change channels, tone map, or apply gamma correction.

```cpp
auto receipt = producer.stream() << image.copy_from(upload) << Horizon::commit();
auto host = image.readback(reader); // CPU-visible bytes on return
// Consume host.pixels using host.format, host.row_pitch and host.slice_pitch.
```

Submit producers before calling readback and register their actual images with
the pipeline bindings. Descriptor indices alone do not register resource use.
For several bindless image members sharing a descriptor array, preserve each
member's reflected push-constant offset when binding; the Vulkan backend tracks
the `(set, binding, byte_offset)` tuple and replaces only the same member.

Readback requires `ImageUsage_TransferSrc`, a valid mip/layer and an uncompressed,
single-sampled color format. Unsupported descriptions throw before submission.
The API allocates a transfer-destination, CPU-readable staging buffer, records
the image layout transition and copy, emits a transfer-to-host buffer barrier,
waits for CPU completion with `wait_idle(receipt)`, invalidates mapped memory,
then copies the bytes into the returned vector. `wait(receipt)` alone only
queues a GPU dependency and is insufficient for host access. Layout-changing
copies serialize with prior image readers as well as writers.

The returned vector owns its pixels independently of the image, executor and
staging allocation. No staging memory is created unless the API is called.
Device failure propagates through the existing executor error behavior; this
API does not introduce a device-loss recovery contract.

Enable `HORIZON_BUILD_HARDWARE_TESTS=ON` to build and register
`horizon_image_readback_tests` / `HorizonImageReadbackTests`. These require a real
Vulkan device and do not silently skip. Coverage includes asymmetric RGBA8 and
sRGB patterns, RGBA16F/RGBA32F bits, mip/layer selection, repeated reads, invalid
inputs, compiler dependencies after shader readers, and multiple bindless image
members in one descriptor array. Engine integration tests additionally dispatch
real shaders between readbacks and verify saved image pixels.
