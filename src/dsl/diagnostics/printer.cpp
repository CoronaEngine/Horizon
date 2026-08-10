//
// Created by Zero on 16/05/2022.
//

#include "printer.h"

namespace ocarina {

namespace {

/**
 * Recover the decodable log span from the downloaded host buffer.
 *
 * The trailing used-word counter can occasionally lag behind the actual
 * record writes, so we start from the declared length and then conservatively
 * scan forward only while full log records are still recognizable.
 */
[[nodiscard]] uint recover_log_length(const Managed<uint> &buffer,
                                      const vector<Printer::Item> &items) noexcept {
    const auto &host = buffer.host_buffer();
    if (host.empty()) {
        return 0u;
    }
    uint capacity = static_cast<uint>(host.size() - 1u);
    uint length = std::min(capacity, host.back());
    while (length < capacity) {
        const uint *data = host.data() + length;
        uint item_index = data[0];
        /** Stop once the next record header no longer maps to a known log item. */
        if (item_index >= items.size()) {
            break;
        }
        uint item_words = items[item_index].size + 1u;
        /** Never extend past the downloaded buffer even if the header looks valid. */
        if (item_words == 0u || length + item_words > capacity) {
            break;
        }
        bool has_non_zero_word = false;
        for (uint index = 0u; index < item_words; ++index) {
            if (data[index] != 0u) {
                has_non_zero_word = true;
                break;
            }
        }
        /** Treat an all-zero region as unused capacity rather than another log record. */
        if (!has_non_zero_word) {
            break;
        }
        length += item_words;
    }
    return length;
}

}// namespace

void Printer::output_log(const OutputFunc &func) noexcept {
    uint length = recover_log_length(buffer_, items_);
    bool truncated = buffer_.host_buffer().back() > length;
    uint offset = 0u;
    while (offset < length) {
        const uint *data = buffer_.host_buffer().data() + offset;
        uint item_index = data[0];
        if (item_index >= items_.size()) {
            truncated = true;
            OC_WARNING_FORMAT("Kernel log decode index {} is out of range {}; stopping log decode.",
                              item_index,
                              items_.size());
            break;
        }
        Item &item = items_[item_index];
        offset += item.size + 1;
        if (offset > length) {
            truncated = true;
        } else {
            item.func(data + 1, func);
        }
    }
    if (truncated) [[unlikely]] {
        OC_WARNING("Kernel log truncated.");
    }
}

CommandBatch Printer::retrieve(const OutputFunc &func) noexcept {
    if (!enabled_) {
        return {};
    }
    CommandBatch ret;
    ret << buffer_.download();
    ret << [&]() {
        output_log(func);
        buffer_.resize(buffer_.capacity());
    };
    ret << buffer_.device_buffer().reset();
    return ret;
}

void Printer::retrieve_immediately(const OutputFunc &func) noexcept {
    if (!enabled_) {
        return;
    }
    buffer_.download_immediately();
    output_log(func);
    reset();
}
}// namespace ocarina