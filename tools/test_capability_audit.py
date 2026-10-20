import importlib.util
from pathlib import Path
import tempfile
import unittest


_path = Path(__file__).resolve().parents[1] / "modules/ocarina/tests/run_capability_audit.py"
_spec = importlib.util.spec_from_file_location("capability_audit", _path)
audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(audit)


class CapabilityAuditTests(unittest.TestCase):
    def compare(self, ast_lines, ir_lines):
        with tempfile.TemporaryDirectory() as directory:
            results = []
            for codegen, lines in (("ast", ast_lines), ("ir", ir_lines)):
                log = Path(directory) / f"{codegen}.log"
                log.write_text("\n".join(lines), encoding="utf-8")
                results.append(dict(case="numeric-probe", codegen=codegen, log=str(log)))
            return audit.compare_observations(results)

    def test_matching_codegen_results_form_one_comparison(self):
        rows = ["FLOAT_MATH op=round input=0.5 actual=1", "FLOAT_MATH op=round input=-0.5 actual=-1"]
        comparisons = self.compare(rows, rows)
        self.assertEqual(len(comparisons), 1)
        self.assertEqual(comparisons[0]["ast_rows"], 2)
        self.assertEqual(comparisons[0]["ir_rows"], 2)
        self.assertEqual(comparisons[0]["different_rows"], 0)

    def test_difference_and_missing_row_are_reported(self):
        comparisons = self.compare(
            ["FLOAT_MATH op=round input=0.5 actual=1", "FLOAT_MATH op=round input=1.5 actual=2"],
            ["FLOAT_MATH op=round input=0.5 actual=0"],
        )
        self.assertEqual(comparisons[0]["different_rows"], 2)
        self.assertEqual(comparisons[0]["max_numeric_delta"], 1)

    def test_unparseable_observation_is_an_error(self):
        comparisons = self.compare(["FLOAT_MATH op=round input=0.5 actual=invalid"],
                                   ["FLOAT_MATH op=round input=0.5 actual=1"])
        self.assertEqual(comparisons[0]["parse_errors"][0]["codegen"], "ast")


if __name__ == "__main__":
    unittest.main()
