"""Regression checks for compiler invocation, stage isolation and output gating."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPILER = Path(sys.argv.pop(1)).resolve()


class FrontendCLI(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="rustless-cli-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def source(self, name, text):
        path = self.root / name
        path.write_text(text, encoding="utf-8")
        return path

    def run_compiler(self, *args):
        return subprocess.run([str(COMPILER), *map(str, args)],
                              capture_output=True, text=True, timeout=10)

    def test_help_and_invocation_errors(self):
        self.assertEqual(self.run_compiler("--help").returncode, 0)
        for args in [(), ("--stage",), ("--unknown",),
                     ("--stage", "codegen", "missing.rx"),
                     ("--parse-entry", "unknown", "missing.rx"),
                     ("missing.rx",), ("a.rx", "b.rx")]:
            with self.subTest(args=args):
                self.assertEqual(self.run_compiler(*args).returncode, 2)

    def test_stage_isolation(self):
        source = self.source("wrong-type.rx", "fn main() { let x: i32 = true; }")
        for stage, expected in [("lex", 0), ("parse", 0), ("semantic", 1)]:
            self.assertEqual(self.run_compiler("--stage", stage, source).returncode, expected)
        self.assertEqual(self.run_compiler(source).returncode, 1)
        invalid = self.source("invalid-token.rx", "@")
        self.assertEqual(self.run_compiler("--stage", "lex", invalid).returncode, 1)

    def test_fragment_entries_and_trailing_tokens(self):
        for entry, text in [("crate", "fn main() {}"), ("item", "struct S {}"),
                            ("expression", "1 + 2"), ("letStatement", "let x = 1;"),
                            ("typeRef", "&&mut [i32; 2]")]:
            with self.subTest(entry=entry):
                source = self.source("fragment.rx", text)
                result = self.run_compiler("--stage", "parse", "--parse-entry", entry, source)
                self.assertEqual(result.returncode, 0, result.stderr)
        trailing = self.source("trailing.rx", "1 2")
        self.assertEqual(self.run_compiler("--stage", "parse", "--parse-entry",
                                           "expression", trailing).returncode, 1)

    def test_dumps_and_rejected_output(self):
        valid = self.source("valid.rx", "fn main() {}")
        result = self.run_compiler("--stage", "parse", "--dump-ast", valid)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Crate", result.stdout)
        output = self.root / "typed.txt"
        result = self.run_compiler("--dump-typed", "-o", output, valid)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(output.read_text(encoding="utf-8"))
        rejected = self.source("rejected.rx", "fn main() { let x: i32 = true; }")
        absent = self.root / "rejected.txt"
        self.assertEqual(self.run_compiler("--dump-typed", "-o", absent, rejected).returncode, 1)
        self.assertFalse(absent.exists())
        self.assertEqual(self.run_compiler("--stage", "lex", "--dump-typed", valid).returncode, 2)


if __name__ == "__main__":
    unittest.main()
