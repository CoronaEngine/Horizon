set(HORIZON_BENCHMARK_COMMON_SOURCES
    common/benchmark_main.cpp
)

horizon_add_benchmark(HorizonSmokeBenchmarks
    ${HORIZON_BENCHMARK_COMMON_SOURCES}
    smoke/smoke_benchmark.cpp
)
