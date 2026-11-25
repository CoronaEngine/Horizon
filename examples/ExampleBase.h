#pragma once

struct ExampleBase {
    explicit ExampleBase(void*) {}
    virtual ~ExampleBase() = default;
    virtual void run(float seconds) = 0;
};
