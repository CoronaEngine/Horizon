#pragma once

#include "hardware_wrapper_vulkan/hardware/execution.h"

#include <tuple>
#include <type_traits>

namespace Corona::Horizon
{
    namespace CommandDetail
    {
        template <typename T>
        struct IsSharedPtr : std::false_type
        {
        };

        template <typename T>
        struct IsSharedPtr<std::shared_ptr<T>> : std::true_type
        {
        };

        template <typename T>
        inline constexpr bool is_shared_ptr_v = IsSharedPtr<std::remove_cvref_t<T>>::value;

        template <typename...>
        inline constexpr bool single_shared_ptr_v = false;

        template <typename T>
        inline constexpr bool single_shared_ptr_v<T> = is_shared_ptr_v<T>;

        template <typename Command>
        [[nodiscard]] StreamCommand make_stream_command(Command command)
        {
            return StreamCommand([command = std::move(command)](CommandRecorder& recorder) {
                command.record(recorder);
            });
        }
    }

    struct CopyBufferCommand
    {
        BufferRef src {};
        BufferRef dst {};
        CopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] BufferRef source() const noexcept { return src; }
        [[nodiscard]] BufferRef destination() const noexcept { return dst; }
        [[nodiscard]] CopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.copy(src, dst, region, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct CopyBufferToImageCommand
    {
        BufferRef src {};
        ImageRef dst {};
        BufferImageCopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] BufferRef source() const noexcept { return src; }
        [[nodiscard]] ImageRef destination() const noexcept { return dst; }
        [[nodiscard]] BufferImageCopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.copy_to_image(src, dst, region, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct ShaderDispatchCommand
    {
        ShaderRef shader {};
        DispatchDesc dispatch {};
        DeviceMask devices {};

        [[nodiscard]] ShaderRef shader_ref() const noexcept { return shader; }
        [[nodiscard]] DispatchDesc dispatch_desc() const noexcept { return dispatch; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.dispatch(shader, dispatch, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct BeginRenderingCommand
    {
        RenderingDesc rendering {};
        DeviceMask devices {};

        [[nodiscard]] RenderingDesc rendering_desc() const noexcept { return rendering; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.begin_rendering(rendering, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct EndRenderingCommand
    {
        DeviceMask devices {};

        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.end_rendering(devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct DrawIndexedCommand
    {
        BufferRef index {};
        BufferRef vertex {};
        DrawIndexedDesc draw {};
        DeviceMask devices {};

        [[nodiscard]] BufferRef index_buffer() const noexcept { return index; }
        [[nodiscard]] BufferRef vertex_buffer() const noexcept { return vertex; }
        [[nodiscard]] DrawIndexedDesc draw_desc() const noexcept { return draw; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.draw_indexed(index, vertex, draw, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct PresentCommand
    {
        DisplayerRef displayer {};
        ImageRef image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };

        [[nodiscard]] DisplayerRef displayer_ref() const noexcept { return displayer; }
        [[nodiscard]] ImageRef image_ref() const noexcept { return image; }
        [[nodiscard]] DeviceId device() const noexcept { return present_device; }
        [[nodiscard]] bool allow_fallback() const noexcept { return allow_cpu_bridge_fallback; }

        void record(CommandRecorder& recorder) const
        {
            recorder.present(displayer, image, present_device, allow_cpu_bridge_fallback);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    class HostFunctionCommand
    {
    public:
        HostFunctionCommand() = default;

        explicit HostFunctionCommand(std::function<void()> callback)
            : callback_(std::move(callback))
        {
        }

        [[nodiscard]] const std::function<void()>& callback() const noexcept { return callback_; }

        void record(CommandRecorder& recorder) const
        {
            recorder.host_callback(callback_);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }

    private:
        std::function<void()> callback_ {};
    };

    class KeepAliveCommand
    {
    public:
        KeepAliveCommand() = default;

        explicit KeepAliveCommand(std::shared_ptr<void> object)
            : object_(std::move(object))
        {
        }

        [[nodiscard]] const std::shared_ptr<void>& object() const noexcept { return object_; }

        void record(CommandRecorder& recorder) const
        {
            recorder.keep_alive(object_);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }

    private:
        std::shared_ptr<void> object_ {};
    };

    [[nodiscard]] inline CopyBufferCommand copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline CopyBufferToImageCommand copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline ShaderDispatchCommand dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {})
    {
        return { shader, desc, devices };
    }

    [[nodiscard]] inline BeginRenderingCommand begin_rendering(RenderingDesc desc, DeviceMask devices = {})
    {
        return { desc, devices };
    }

    [[nodiscard]] inline EndRenderingCommand end_rendering(DeviceMask devices = {})
    {
        return { devices };
    }

    [[nodiscard]] inline DrawIndexedCommand draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {})
    {
        return { index, vertex, desc, devices };
    }

    [[nodiscard]] inline PresentCommand present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true)
    {
        return { displayer, image, present_device, allow_cpu_bridge_fallback };
    }

    [[nodiscard]] inline HostFunctionCommand host_callback(std::function<void()> callback)
    {
        return HostFunctionCommand(std::move(callback));
    }

    [[nodiscard]] inline KeepAliveCommand keep_alive(std::shared_ptr<void> object)
    {
        return KeepAliveCommand(std::move(object));
    }

    template <typename T>
    requires(!std::is_void_v<T>)
    [[nodiscard]] KeepAliveCommand keep_alive(std::shared_ptr<T> object)
    {
        return keep_alive(std::static_pointer_cast<void>(std::move(object)));
    }

    template <typename... Args>
    requires(sizeof...(Args) > 0u &&
             (std::is_copy_constructible_v<std::remove_cvref_t<Args>> && ...) &&
             !CommandDetail::single_shared_ptr_v<Args...>)
    [[nodiscard]] KeepAliveCommand keep_alive(Args&&... args)
    {
        using Storage = std::tuple<std::remove_cvref_t<Args>...>;
        return keep_alive(std::static_pointer_cast<void>(
            std::make_shared<Storage>(std::forward<Args>(args)...)));
    }
}
