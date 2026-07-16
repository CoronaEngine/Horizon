#include "execution_profile.h"

#include "horizon.h"
#include "corona/kernel/core/i_logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string_view>

namespace Corona::Horizon
{
    namespace
    {
        thread_local ExecutionCommitProfileSample* active_sample = nullptr;

        void add_sample(ExecutionCommitProfileSample& target, const ExecutionCommitProfileSample& sample) noexcept
        {
#define HORIZON_ADD_PROFILE_FIELD(name) target.name += sample.name
            HORIZON_ADD_PROFILE_FIELD(compile_ms);
            HORIZON_ADD_PROFILE_FIELD(keep_alive_ms);
            HORIZON_ADD_PROFILE_FIELD(tracker_wait_ms);
            HORIZON_ADD_PROFILE_FIELD(encode_ms);
            HORIZON_ADD_PROFILE_FIELD(queue_retire_ms);
            HORIZON_ADD_PROFILE_FIELD(queue_lock_wait_ms);
            HORIZON_ADD_PROFILE_FIELD(queue_submit_ms);
            HORIZON_ADD_PROFILE_FIELD(present_ms);
            HORIZON_ADD_PROFILE_FIELD(total_ms);
            HORIZON_ADD_PROFILE_FIELD(commands);
            HORIZON_ADD_PROFILE_FIELD(logical_draws);
            HORIZON_ADD_PROFILE_FIELD(draw_batches);
            HORIZON_ADD_PROFILE_FIELD(resource_uses);
            HORIZON_ADD_PROFILE_FIELD(descriptor_pool_creates);
            HORIZON_ADD_PROFILE_FIELD(uniform_buffer_allocations);
#undef HORIZON_ADD_PROFILE_FIELD
        }

        void maximize_sample(ExecutionCommitProfileSample& target, const ExecutionCommitProfileSample& sample) noexcept
        {
#define HORIZON_MAX_PROFILE_FIELD(name) target.name = std::max(target.name, sample.name)
            HORIZON_MAX_PROFILE_FIELD(compile_ms);
            HORIZON_MAX_PROFILE_FIELD(keep_alive_ms);
            HORIZON_MAX_PROFILE_FIELD(tracker_wait_ms);
            HORIZON_MAX_PROFILE_FIELD(encode_ms);
            HORIZON_MAX_PROFILE_FIELD(queue_retire_ms);
            HORIZON_MAX_PROFILE_FIELD(queue_lock_wait_ms);
            HORIZON_MAX_PROFILE_FIELD(queue_submit_ms);
            HORIZON_MAX_PROFILE_FIELD(present_ms);
            HORIZON_MAX_PROFILE_FIELD(total_ms);
            HORIZON_MAX_PROFILE_FIELD(commands);
            HORIZON_MAX_PROFILE_FIELD(logical_draws);
            HORIZON_MAX_PROFILE_FIELD(draw_batches);
            HORIZON_MAX_PROFILE_FIELD(resource_uses);
            HORIZON_MAX_PROFILE_FIELD(descriptor_pool_creates);
            HORIZON_MAX_PROFILE_FIELD(uniform_buffer_allocations);
#undef HORIZON_MAX_PROFILE_FIELD
        }

        void divide_sample(ExecutionCommitProfileSample& sample, std::uint64_t divisor) noexcept
        {
            if (divisor == 0)
                return;
#define HORIZON_DIV_PROFILE_FIELD(name) sample.name /= static_cast<double>(divisor)
            HORIZON_DIV_PROFILE_FIELD(compile_ms);
            HORIZON_DIV_PROFILE_FIELD(keep_alive_ms);
            HORIZON_DIV_PROFILE_FIELD(tracker_wait_ms);
            HORIZON_DIV_PROFILE_FIELD(encode_ms);
            HORIZON_DIV_PROFILE_FIELD(queue_retire_ms);
            HORIZON_DIV_PROFILE_FIELD(queue_lock_wait_ms);
            HORIZON_DIV_PROFILE_FIELD(queue_submit_ms);
            HORIZON_DIV_PROFILE_FIELD(present_ms);
            HORIZON_DIV_PROFILE_FIELD(total_ms);
#undef HORIZON_DIV_PROFILE_FIELD
            sample.commands /= divisor;
            sample.logical_draws /= divisor;
            sample.draw_batches /= divisor;
            sample.resource_uses /= divisor;
            sample.descriptor_pool_creates /= divisor;
            sample.uniform_buffer_allocations /= divisor;
        }
    }

    void ExecutionCommitProfileWindow::add(const ExecutionCommitProfileSample& sample) noexcept
    {
        ++samples_;
        add_sample(sum_, sample);
        maximize_sample(max_, sample);
    }

    ExecutionCommitProfileSnapshot ExecutionCommitProfileWindow::snapshot() const noexcept
    {
        ExecutionCommitProfileSnapshot result;
        result.samples = samples_;
        result.avg = sum_;
        result.max = max_;
        divide_sample(result.avg, samples_);
        return result;
    }

    void ExecutionCommitProfileWindow::clear() noexcept
    {
        samples_ = 0;
        sum_ = {};
        max_ = {};
    }

    ExecutionCommitProfileScope::ExecutionCommitProfileScope(ExecutionCommitProfileSample& sample) noexcept
        : previous_(active_sample)
    {
        active_sample = &sample;
    }

    ExecutionCommitProfileScope::~ExecutionCommitProfileScope()
    {
        active_sample = previous_;
    }

    bool execution_commit_profile_enabled(const char* value) noexcept
    {
        if (value == nullptr || *value == '\0')
            return false;
        const std::string_view setting(value);
        return setting != "0" && setting != "false" && setting != "FALSE" && setting != "off" && setting != "OFF";
    }

    void note_descriptor_pool_create() noexcept
    {
        if (active_sample != nullptr)
            ++active_sample->descriptor_pool_creates;
    }

    void note_uniform_buffer_allocation() noexcept
    {
        if (active_sample != nullptr)
            ++active_sample->uniform_buffer_allocations;
    }

    void record_execution_commit_profile(const ExecutionCommitProfileSample& sample)
    {
        if (!execution_commit_profile_enabled(std::getenv("HORIZON_EXECUTION_DIAG_PROFILE")))
            return;

        using Clock = std::chrono::steady_clock;
        static std::mutex mutex;
        static std::array<ExecutionCommitProfileWindow, 3> windows;
        static Clock::time_point last_report = Clock::now();

        std::lock_guard lock(mutex);
        const auto size = classify_execution_commit(sample.commands, sample.logical_draws);
        windows[static_cast<std::size_t>(size)].add(sample);
        const Clock::time_point now = Clock::now();
        if (now - last_report < std::chrono::seconds(1))
            return;

        constexpr std::array<std::string_view, 3> names{"small", "medium", "large"};
        for (std::size_t i = 0; i < windows.size(); ++i)
        {
            const ExecutionCommitProfileSnapshot report = windows[i].snapshot();
            if (report.samples == 0)
                continue;
            CFW_LOG_INFO("[HorizonExecutionPerf] class={} samples={} avg_total_ms={:.3f} max_total_ms={:.3f} "
                         "avg_compile_ms={:.3f} avg_keep_alive_ms={:.3f} avg_tracker_wait_ms={:.3f} "
                         "avg_encode_ms={:.3f} avg_queue_retire_ms={:.3f} avg_queue_lock_wait_ms={:.3f} "
                         "avg_vk_submit_ms={:.3f} avg_present_ms={:.3f} avg_commands={} avg_logical_draws={} "
                         "avg_draw_batches={} avg_resource_uses={} "
                         "avg_descriptor_pools={} avg_ubo_allocations={}",
                         names[i], report.samples, report.avg.total_ms, report.max.total_ms,
                         report.avg.compile_ms, report.avg.keep_alive_ms, report.avg.tracker_wait_ms,
                         report.avg.encode_ms, report.avg.queue_retire_ms, report.avg.queue_lock_wait_ms,
                         report.avg.queue_submit_ms, report.avg.present_ms, report.avg.commands,
                         report.avg.logical_draws, report.avg.draw_batches, report.avg.resource_uses,
                         report.avg.descriptor_pool_creates,
                         report.avg.uniform_buffer_allocations);
            windows[i].clear();
        }
        last_report = now;
    }
}
