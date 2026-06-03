//#pragma once
//
//#include "format.h"
//#include "resource.h"
//
//#include <cstddef>
//#include <cstdint>
//#include <functional>
//#include <memory>
//#include <tuple>
//#include <type_traits>
//#include <utility>
//#include <vector>
//
//namespace Corona::Horizon
//{
//    struct DeviceId
//    {
//        uint32_t value { 0 };
//
//        [[nodiscard]] friend bool operator==(DeviceId left, DeviceId right) noexcept
//        {
//            return left.value == right.value;
//        }
//    };
//
//    
//    class StreamCommand
//    {
//    public:
//        StreamCommand() = default;
//        explicit StreamCommand(std::function<void(class CommandRecorder&)> recorder);
//
//        void record(class CommandRecorder& recorder) const;
//        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(recorder_); }
//
//    private:
//        std::function<void(class CommandRecorder&)> recorder_ {};
//    };
//
//    struct CopyBufferCommand
//    {
//        BufferRef src {};
//        BufferRef dst {};
//        CopyRegion region {};
//        DeviceMask devices {};
//
//        [[nodiscard]] BufferRef source() const noexcept { return src; }
//        [[nodiscard]] BufferRef destination() const noexcept { return dst; }
//        [[nodiscard]] CopyRegion copy_region() const noexcept { return region; }
//        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }
//
//        void record(CommandRecorder& recorder) const
//        {
//            recorder.copy(src, dst, region, devices);
//        }
//
//        [[nodiscard]] StreamCommand stream_command() const
//        {
//            return CommandDetail::make_stream_command(*this);
//        }
//
//        [[nodiscard]] operator StreamCommand() const
//        {
//            return stream_command();
//        }
//    };
//
//    struct CopyBufferToImageCommand
//    {
//        BufferRef src {};
//        ImageRef dst {};
//        BufferImageCopyRegion region {};
//        DeviceMask devices {};
//
//        [[nodiscard]] BufferRef source() const noexcept { return src; }
//        [[nodiscard]] ImageRef destination() const noexcept { return dst; }
//        [[nodiscard]] BufferImageCopyRegion copy_region() const noexcept { return region; }
//        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }
//
//        void record(CommandRecorder& recorder) const
//        {
//            recorder.copy_to_image(src, dst, region, devices);
//        }
//
//        [[nodiscard]] StreamCommand stream_command() const
//        {
//            return CommandDetail::make_stream_command(*this);
//        }
//
//        [[nodiscard]] operator StreamCommand() const
//        {
//            return stream_command();
//        }
//    };
//
//    struct CopyImageCommand
//    {
//        ImageRef src {};
//        ImageRef dst {};
//        ImageCopyRegion region {};
//        DeviceMask devices {};
//
//        [[nodiscard]] ImageRef source() const noexcept { return src; }
//        [[nodiscard]] ImageRef destination() const noexcept { return dst; }
//        [[nodiscard]] ImageCopyRegion copy_region() const noexcept { return region; }
//        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }
//
//        void record(CommandRecorder& recorder) const
//        {
//            recorder.copy_image(src, dst, region, devices);
//        }
//
//        [[nodiscard]] StreamCommand stream_command() const
//        {
//            return CommandDetail::make_stream_command(*this);
//        }
//
//        [[nodiscard]] operator StreamCommand() const
//        {
//            return stream_command();
//        }
//    };
//
//    struct CopyImageToBufferCommand
//    {
//        ImageRef src {};
//        BufferRef dst {};
//        BufferImageCopyRegion region {};
//        DeviceMask devices {};
//
//        [[nodiscard]] ImageRef source() const noexcept { return src; }
//        [[nodiscard]] BufferRef destination() const noexcept { return dst; }
//        [[nodiscard]] BufferImageCopyRegion copy_region() const noexcept { return region; }
//        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }
//
//        void record(CommandRecorder& recorder) const
//        {
//            recorder.copy_to_buffer(src, dst, region, devices);
//        }
//
//        [[nodiscard]] StreamCommand stream_command() const
//        {
//            return CommandDetail::make_stream_command(*this);
//        }
//
//        [[nodiscard]] operator StreamCommand() const
//        {
//            return stream_command();
//        }
//    };
//}
