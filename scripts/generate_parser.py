#!/usr/bin/env python3
"""Regenerate the checked-in ANTLR C++ sources using a local 4.13.2 JAR."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


VERSION = "4.13.2"
JAR_SHA256 = "eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76"
OUTPUTS = ("RxLexer.cpp", "RxLexer.h", "RxParser.cpp", "RxParser.h")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--antlr-jar", type=Path, required=True)
    parser.add_argument("--java", default="java")
    parser.add_argument("--check", action="store_true",
                        help="verify generated files without changing them")
    args = parser.parse_args()
    jar = args.antlr_jar.resolve()
    if not jar.is_file() or hashlib.sha256(jar.read_bytes()).hexdigest() != JAR_SHA256:
        parser.error("provide the official antlr-4.13.2-complete.jar (SHA256 mismatch)")
    project = Path(__file__).resolve().parents[1]
    destination = project / "generated" / "antlr"
    manifest = {"antlr_version": VERSION}
    with tempfile.TemporaryDirectory(prefix="rustless-antlr-") as temporary:
        workspace = Path(temporary)
        for name in ("Lexer", "Parser"):
            source = project / "grammar" / f"{name}.g4"
            grammar = source.read_text(encoding="utf-8")
            manifest[f"{name.lower()}_sha256"] = hashlib.sha256(grammar.encode("utf-8")).hexdigest()
            grammar = grammar.replace(f"grammar {name};", f"grammar Rx{name};")
            grammar = grammar.replace("tokenVocab=Lexer", "tokenVocab=RxLexer")
            (workspace / f"Rx{name}.g4").write_text(grammar, encoding="utf-8", newline="\n")
        subprocess.run([
            args.java, "-jar", str(jar), "-Dlanguage=Cpp", "-package", "rxgrammar",
            "-no-listener", "-Xexact-output-dir", "-o", "output",
            "RxLexer.g4", "RxParser.g4",
        ], cwd=workspace, check=True)
        output = workspace / "output"
        (output / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
        for name in (*OUTPUTS, "manifest.json"):
            generated = (output / name).read_bytes().replace(b"\r\n", b"\n")
            target = destination / name
            if args.check:
                if not target.is_file() or target.read_bytes() != generated:
                    parser.exit(1, f"outdated generated file: {target}\n")
            else:
                destination.mkdir(parents=True, exist_ok=True)
                target.write_bytes(generated)
    print("ANTLR generated sources verified" if args.check else "ANTLR sources regenerated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
