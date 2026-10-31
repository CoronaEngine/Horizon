from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]


class CoreContracts(unittest.TestCase):
    def test_core_header_keeps_api_macros_portable(self) -> None:
        header = (REPO_ROOT / "src/core/header.h").read_text(encoding="utf-8")

        self.assertNotIn('#include "core/runtime/oc_windows.h"', header)
        self.assertIn(
            "#if defined(_MSC_VER) && !defined(OC_STATIC_LINK)\n"
            "#define OC_DLL_EXPORT __declspec(dllexport)",
            header,
        )
        self.assertIn('#define OC_DLL_EXPORT [[gnu::visibility("default")]]', header)
        self.assertIn("#define OC_DLL_EXPORT\n#define OC_DLL_IMPORT\n#endif", header)

    def test_core_stl_does_not_call_msvc_allocation_or_conversion_apis(self) -> None:
        header = (REPO_ROOT / "src/core/stl.h").read_text(encoding="utf-8")
        stl = header + (REPO_ROOT / "src/core/stl.cpp").read_text(encoding="utf-8")

        self.assertIn("#include <cstring>", header)
        self.assertNotIn("_aligned_malloc", stl)
        self.assertNotIn("_aligned_free", stl)
        self.assertNotIn("wcstombs_s", stl)
        self.assertNotIn("std::wmemcpy", stl)

    def test_core_type_source_includes_its_ranges_algorithm_dependency(self) -> None:
        source = (REPO_ROOT / "src/core/type_system/type.cpp").read_text(encoding="utf-8")

        self.assertIn("#include <algorithm>", source)
        self.assertIn("std::ranges::any_of", source)

    def test_core_size_literals_use_the_standard_integer_parameter_type(self) -> None:
        header = (REPO_ROOT / "src/core/util/util.h").read_text(encoding="utf-8")

        for suffix in ("kb", "mb", "gb"):
            self.assertIn(f'operator""_{suffix}(unsigned long long bytes)', header)

    def test_math_scalar_functions_include_the_standard_math_header(self) -> None:
        header = (REPO_ROOT / "src/math/scalar_func.h").read_text(encoding="utf-8")

        self.assertIn("#include <cmath>", header)

    def test_dsl_host_size_calculations_use_host_min_and_ulong_is_scalar(self) -> None:
        soa = (REPO_ROOT / "src/dsl/types/soa.h").read_text(encoding="utf-8")
        traits = (REPO_ROOT / "src/math/basic_traits.h").read_text(encoding="utf-8")

        self.assertIn("#include <algorithm>", soa)
        self.assertIn("std::is_same<std::remove_cvref_t<T>, ulong>", traits)
        self.assertIn("std::min(view_size, uint(buffer_view_.size_in_byte()))", soa)
        self.assertIn("std::min(uint(buffer.size_in_byte()), view_size)", soa)


if __name__ == "__main__":
    unittest.main()
