"""Check offline dependency integrity and rejection of stale grammar snapshots."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
CMAKE, GENERATOR = sys.argv[1:3]
del sys.argv[1:3]


class OfflineInputs(unittest.TestCase):
    def test_grammar_fingerprints(self):
        manifest = json.loads((PROJECT / "generated/antlr/manifest.json").read_text())
        self.assertEqual(manifest["antlr_version"], "4.13.2")
        for name in ("Lexer", "Parser"):
            grammar = (PROJECT / f"grammar/{name}.g4").read_text(encoding="utf-8")
            self.assertEqual(hashlib.sha256(grammar.encode()).hexdigest(),
                             manifest[f"{name.lower()}_sha256"])

    def test_runtime_archive(self):
        archive = PROJECT / "vendor/antlr/antlr4-cpp-runtime-4.13.2-source.zip"
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         "0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4")

    def test_stale_grammar_rejected_before_dependency_setup(self):
        with tempfile.TemporaryDirectory(prefix="rustless-stale-") as temporary:
            fixture = Path(temporary)
            for name in ("grammar/Lexer.g4", "grammar/Parser.g4", "cmake/Antlr.cmake",
                         "generated/antlr/manifest.json"):
                target = fixture / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(PROJECT / name, target)
            grammar = fixture / "grammar/Lexer.g4"
            grammar.write_text(grammar.read_text(encoding="utf-8") + "\n// changed\n",
                               encoding="utf-8")
            (fixture / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.24)\n"
                "project(stale_snapshot LANGUAGES NONE)\ninclude(cmake/Antlr.cmake)\n")
            process = subprocess.run(
                [CMAKE, "-S", str(fixture), "-B", str(fixture / "build"), "-G", GENERATOR],
                capture_output=True, text=True, timeout=30)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("Lexer.g4 changed", process.stdout + process.stderr)


if __name__ == "__main__":
    unittest.main()
