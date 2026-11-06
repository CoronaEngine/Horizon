#pragma once

#include "horizon/hotfix/process_runner.h"

namespace horizon::hotfix {

// Starts an explicit POSIX executable without PATH lookup or shell parsing.
// Standard input is EOF. Output is captured as raw bytes until the direct child exits.
class PosixProcessRunner final : public IProcessRunner {
public:
    [[nodiscard]] ProcessResult run(const ProcessRequest &request) override;
};

}// namespace horizon::hotfix
