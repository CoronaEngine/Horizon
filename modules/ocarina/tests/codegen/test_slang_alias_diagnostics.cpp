#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <slang.h>
#include <slang-com-ptr.h>
#include <iostream>

using namespace ocarina;

struct SlangAliasRecord { float value; };
OC_STRUCT(, SlangAliasRecord, value) {};

namespace {

bool compile_source(const string &source, string &diagnostic_text) {
    auto &context = RHIContext::instance();
    Slang::ComPtr<slang::IGlobalSession> global;
    if (SLANG_FAILED(slang::createGlobalSession(global.writeRef()))) return false;
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = global->findProfile("spirv_1_5");
    const string includes = (context.runtime_directory() / "slang").string();
    const char *paths[]{includes.c_str()};
    slang::CompilerOptionEntry warning{};
    warning.name = slang::CompilerOptionName::WarningsAsErrors;
    warning.value.kind = slang::CompilerOptionValueKind::String;
    // In Slang 2026.10 a per-ID severity override supersedes source pragmas.
    // The global warning-as-error flag honors scoped suppression instead.
    warning.value.stringValue0 = "all";
    slang::SessionDesc description{};
    description.targets = &target;
    description.targetCount = 1;
    description.searchPaths = paths;
    description.searchPathCount = 1;
    description.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    description.compilerOptionEntries = &warning;
    description.compilerOptionEntryCount = 1;
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global->createSession(description, session.writeRef()))) return false;
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    module = session->loadModuleFromSourceString("alias_regression", nullptr,
                                                source.c_str(), diagnostics.writeRef());
    diagnostic_text = diagnostics ? static_cast<const char *>(diagnostics->getBufferPointer()) : "";
    return module != nullptr;
}

}// namespace

int main() {
    // The annotation still needs physical line boundaries when other source is compacted.
    Env::set_code_obfuscation(true);
    auto device = create_test_device();
    Kernel kernel = [](BufferVar<float> out) {
        Callable mutate = [](Float &first, Float &second) {
            first = 1.f;
            second = 2.f;
            return first;
        };
        Callable mutate_void = [](Float &first, Float &second) {
            first += 1.f;
            second += first;
        };
        Float shared = 2.f;
        mutate_void(shared, shared);
        out.write(0u, mutate(shared, shared));
        Var<SlangAliasRecord> first, second;
        first.value = 3.f;
        second.value = 4.f;
        // Slang 2026.10 mistakes the same member of distinct objects for aliases.
        out.write(1u, mutate(first.value, second.value));
    };
    auto shader = device.compile(kernel, "slang-reference-alias-diagnostics");
    string source = RHIContext::instance().read_global_cache(kernel.function()->func_name() + ".slang");
    string diagnostics;
    if (source.empty() || !compile_source(source, diagnostics)) {
        std::cerr << "generated reference calls failed strict alias diagnostics\n" << diagnostics << std::endl;
        return EXIT_FAILURE;
    }
    const string pragma = "#pragma warning(suppress : 30051)";
    if (source.find(pragma) == string::npos) {
        std::cerr << "reference calls have no scoped alias annotation" << std::endl;
        return EXIT_FAILURE;
    }
    string unannotated = source;
    for (auto at = unannotated.find(pragma); at != string::npos; at = unannotated.find(pragma))
        unannotated.erase(at, pragma.size());
    if (compile_source(unannotated, diagnostics) || diagnostics.find("E30051") == string::npos) {
        std::cerr << "unannotated aliases were not diagnosed\n" << diagnostics << std::endl;
        return EXIT_FAILURE;
    }
    // A true out/inout hazard after generated calls must remain an error.
    source += R"(
void unrelated_inout(inout float first, inout float second) { first = 1.f; second = 2.f; }
void unrelated_alias() { float value = 0.f; unrelated_inout(value, value); }
)";
    if (compile_source(source, diagnostics) || diagnostics.find("E30051") == string::npos) {
        std::cerr << "alias annotation leaked to unrelated inout calls\n" << diagnostics << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "Slang reference aliases compile strictly; unrelated inout aliases remain diagnosed" << std::endl;
}
