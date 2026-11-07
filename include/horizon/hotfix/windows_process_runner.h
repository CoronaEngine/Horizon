#pragma once

#include "horizon/hotfix/process_runner.h"

namespace horizon::hotfix {

// Starts a native Windows executable with MSVC-compatible argument quoting.
// Standard input is EOF. Standard output and error are captured as raw bytes.
// Returns after the direct child exits and its queued output has been read.
class WindowsProcessRunner final : public IProcessRunner {
public:
    [[nodiscard]] ProcessResult run(const ProcessRequest &request) override;
};

}// namespace horizon::hotfix
