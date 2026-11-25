#pragma once

#include "../../ExampleBase.h"

void run_example_baseline_tutorial();

class BaselineTutorial final : public ExampleBase {
public:
    using ExampleBase::ExampleBase;

    void run() override
    {
        run_example_baseline_tutorial();
    }
};
