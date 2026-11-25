#pragma once

struct ExampleBase {
    explicit ExampleBase(void*) {}
    virtual ~ExampleBase() = default;
    virtual void run() = 0;
};
