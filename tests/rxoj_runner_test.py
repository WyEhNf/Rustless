import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import test as runner


class JudgeSuites(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="rustless-rxoj-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "program.rx").write_text("fn main() {}", encoding="utf-8")
        (self.root / "program.out").write_bytes(b"")
        entries = [
            {"source": "program.rx", "stage": "semantic", "compilation_success": True},
            {"source": "program.rx", "stage": "semantic", "compilation_success": False},
            {"source": "program.rx", "stage": "codegen", "compilation_success": True,
             "io": [{"input": None, "output": "program.out"}]},
            {"source": "program.rx", "stage": "optimization", "compilation_success": True,
             "io": [{"input": None, "output": "program.out"}]},
        ]
        (self.root / "manifest.json").write_text(json.dumps(entries), encoding="utf-8")
        self.cases = runner.discover(self.root)

    def stages(self, **selection):
        return [case.stage for case in runner.select_stages(self.cases, **selection)]

    def test_ir_suite_includes_positive_and_negative_semantics(self):
        selected = runner.select_stages(self.cases, suite="ir")
        self.assertEqual([case.stage for case in selected], ["semantic", "semantic", "ir"])
        self.assertEqual([case.success for case in selected], [True, False, True])
        self.assertEqual(selected[-1].io, [(None, self.root / "program.out")])

    def test_single_ir_stage_preserves_existing_selection(self):
        self.assertEqual(self.stages(stage="ir"), ["ir"])

    def test_codegen_suite_includes_ir(self):
        self.assertEqual(self.stages(suite="codegen"),
                         ["semantic", "semantic", "ir", "codegen"])

    def test_optimization_suite_includes_all_stages(self):
        self.assertEqual(self.stages(suite="optimization"),
                         ["semantic", "semantic", "ir", "codegen", "optimization"])

    def test_semantic_suite_stops_before_runtime_stages(self):
        self.assertEqual(self.stages(suite="semantic"), ["semantic", "semantic"])

    def test_stage_and_suite_conflict(self):
        with self.assertRaisesRegex(runner.TestError, "either STAGE or SUITE"):
            runner.select_stages(self.cases, stage="ir", suite="ir")

    def test_invalid_or_empty_selection_is_an_error(self):
        with self.assertRaises(runner.TestError):
            runner.select_stages(self.cases, suite="unknown")
        with self.assertRaises(runner.TestError):
            runner.select_stages([], suite="ir")


if __name__ == "__main__":
    unittest.main()
