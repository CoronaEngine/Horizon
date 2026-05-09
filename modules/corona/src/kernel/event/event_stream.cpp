#include <cassert>

#include "corona/kernel/event/i_event_stream.h"

namespace Corona::Kernel {

// EventBusStream implementation
class EventBusStream : public IEventBusStream {
   public:
    EventBusStream() = default;
    ~EventBusStream() override = default;

    EventBusStream(const EventBusStream&) = delete;
    EventBusStream& operator=(const EventBusStream&) = delete;

   protected:
    std::shared_ptr<void> get_stream_impl(std::type_index type) override {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(type);
        return it != streams_.end() ? it->second : nullptr;
    }

    std::shared_ptr<void> get_or_create_stream_impl(std::type_index type,
                                                    std::shared_ptr<void> new_stream) override {
        std::lock_guard<std::mutex> lock(streams_mutex_);

        // Double-check: another thread might have created it
        auto it = streams_.find(type);
        if (it != streams_.end()) {
            return it->second;  // Return existing stream
        }

        // Insert new stream
        streams_[type] = new_stream;
        return new_stream;
    }

   private:
    std::mutex streams_mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> streams_;
};

// Factory function
std::unique_ptr<IEventBusStream> create_event_bus_stream() {
    return std::make_unique<EventBusStream>();
}

}  // namespace Corona::Kernel
