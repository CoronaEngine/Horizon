//
// Created by Zero on 2024/7/30.
//

#pragma once

#include <windows.h>
#include <string_view>

namespace vision::inline hotfix {

// The build worker owns this shell so that toolchain environment changes
// remain available to subsequent compile and link commands.
class CmdProcess {
private:
    HANDLE process_{};
    HANDLE output_read_{};
    HANDLE input_write_{};

    bool initialise() noexcept;
    void cleanup_process() noexcept;

public:
    CmdProcess() = default;
    CmdProcess(const CmdProcess &) = delete;
    CmdProcess &operator=(const CmdProcess &) = delete;
    ~CmdProcess();

    [[nodiscard]] bool run(std::string_view command);
};

}// namespace vision::inline hotfix
