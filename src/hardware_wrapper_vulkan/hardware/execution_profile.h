#pragma once

#include <algorithm>
#include <cstdint>

namespace Corona::Horizon
{
    enum class ExecutionCommitSize : std::uint8_t
    {
        Small,
        Medium,
        Large,
    };

    [[nodiscard]] constexpr ExecutionCommitSize classify_execution_commit(std::uint64_t commands) noexcept
    {
        if (commands >= 1024)
            return ExecutionCommitSize::Large;
        if (commands >= 128)
            return ExecutionCommitSize::Medium;
        return ExecutionCommitSize::Small;
    }

    [[nodiscard]] constexpr ExecutionCommitSize classify_execution_commit(
        std::uint64_t commands, std::uint64_t logical_draws) noexcept
    {
        return classify_execution_commit(std::max(commands, logical_draws));
    }

    struct ExecutionCommitProfileSample
    {
        double compile_ms { 0.0 };
        double keep_alive_ms { 0.0 };
        double tracker_wait_ms { 0.0 };
        double encode_ms { 0.0 };
        double queue_retire_ms { 0.0 };
        double queue_lock_wait_ms { 0.0 };
        double queue_submit_ms { 0.0 };
        double present_ms { 0.0 };
        double total_ms { 0.0 };
        std::uint64_t commands { 0 };
        std::uint64_t logical_draws { 0 };
        std::uint64_t draw_batches { 0 };
        std::uint64_t resource_uses { 0 };
        std::uint64_t descriptor_pool_creates { 0 };
        std::uint64_t uniform_buffer_allocations { 0 };
    };

    struct ExecutionCommitProfileSnapshot
    {
        std::uint64_t samples { 0 };
        ExecutionCommitProfileSample avg {};
        ExecutionCommitProfileSample max {};
    };

    class ExecutionCommitProfileWindow
    {
    public:
        void add(const ExecutionCommitProfileSample& sample) noexcept;
        [[nodiscard]] ExecutionCommitProfileSnapshot snapshot() const noexcept;
        void clear() noexcept;

    private:
        std::uint64_t samples_ { 0 };
        ExecutionCommitProfileSample sum_ {};
        ExecutionCommitProfileSample max_ {};
    };

    class ExecutionCommitProfileScope
    {
    public:
        explicit ExecutionCommitProfileScope(ExecutionCommitProfileSample& sample) noexcept;
        ~ExecutionCommitProfileScope();
        ExecutionCommitProfileScope(const ExecutionCommitProfileScope&) = delete;
        ExecutionCommitProfileScope& operator=(const ExecutionCommitProfileScope&) = delete;

    private:
        ExecutionCommitProfileSample* previous_ { nullptr };
    };

    [[nodiscard]] bool execution_commit_profile_enabled(const char* value) noexcept;
    void record_execution_commit_profile(const ExecutionCommitProfileSample& sample);
    void note_descriptor_pool_create() noexcept;
    void note_uniform_buffer_allocation() noexcept;
}
