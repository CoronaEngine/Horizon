#if defined(_WIN32)
#define OC_PLATFORM_FIXTURE_EXPORT __declspec(dllexport)
#else
#define OC_PLATFORM_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" OC_PLATFORM_FIXTURE_EXPORT int ocarina_platform_fixture_value() {
    return 52;
}
