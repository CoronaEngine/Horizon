#include "example_baseline/example_baseline.h"

#include <exception>
#include <iostream>

int main() {
    try {
        run_example_baseline();
    } catch (const std::exception &error) {
        std::cerr << "Baseline window failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
