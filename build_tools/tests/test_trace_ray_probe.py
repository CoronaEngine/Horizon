import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "src/tests/probes/run_trace_ray_probe.py"


class TraceRayEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("trace_ray_probe_runner", SCRIPT)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def passing_report(self):
        return {"status": "passed", "ray_query_enabled": False,
                "submitted": True, "completed": True, "ray_count": 8,
                "trace_ray_instructions": 2,
                "cases": [{"name": name, "status": "passed", "verified_rays": 8}
                          for name in ("closest-hit", "occlusion")]}

    def test_requires_completed_gpu_dispatch(self):
        report = self.passing_report()
        report["completed"] = False
        self.assertTrue(self.module.validate_result(report, 0))

    def test_rejects_capability_only_success(self):
        report = self.passing_report()
        report.update(status="unsupported", submitted=False, completed=False)
        self.assertTrue(self.module.validate_result(report, 2))

    def test_rejects_ray_query_substitution(self):
        report = self.passing_report()
        report["ray_query_enabled"] = True
        self.assertTrue(self.module.validate_result(report, 0))

    def test_accepts_verified_trace_ray_execution(self):
        self.assertEqual(self.module.validate_result(self.passing_report(), 0), [])

    def test_rejects_missing_occlusion_results(self):
        report = self.passing_report()
        report["cases"].pop()
        self.assertTrue(self.module.validate_result(report, 0))
