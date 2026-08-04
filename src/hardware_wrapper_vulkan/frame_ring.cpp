#include "frame_ring.h"

#include <atomic>

namespace Corona::Horizon
{
    namespace
    {
        std::atomic<uint32_t>& ring_size() noexcept
        {
            // 未建链前为 1：逐帧串行，等价于改造前行为。
            static std::atomic<uint32_t> size { 1 };
            return size;
        }

        std::atomic<uint64_t>& ring_epoch() noexcept
        {
            static std::atomic<uint64_t> epoch { 0 };
            return epoch;
        }
    }

    uint32_t frame_ring_size() noexcept
    {
        const uint32_t size = ring_size().load(std::memory_order_acquire);
        return size == 0 ? 1u : size;
    }

    void publish_frame_ring_size(uint32_t size) noexcept
    {
        if (size == 0)
        {
            return;
        }

        uint32_t current = ring_size().load(std::memory_order_acquire);
        while (size > current)
        {
            if (ring_size().compare_exchange_weak(current,
                                                  size,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
            {
                return;
            }
        }
    }

    uint64_t frame_ring_epoch() noexcept
    {
        return ring_epoch().load(std::memory_order_acquire);
    }

    uint32_t frame_ring_slot() noexcept
    {
        return static_cast<uint32_t>(frame_ring_epoch() % frame_ring_size());
    }

    uint64_t advance_frame_ring() noexcept
    {
        return ring_epoch().fetch_add(1, std::memory_order_acq_rel) + 1;
    }
}
