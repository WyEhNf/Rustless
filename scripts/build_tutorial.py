#!/usr/bin/env python3
"""Assemble the chapter sources into one independently compilable TeX file."""

import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    parts = root / "docs" / "tutorial-parts"
    text = (parts / "preamble.tex").read_text(encoding="utf-8")
    boundaries = {
        1: "由文本建立结构", 4: "用类型组织 AST",
        9: "由结构确定含义", 14: "由含义形成计算",
        19: "由计算走向机器", 22: "检验与扩展设计",
    }
    count = 0
    chapters = (("part-front.tex", 8), ("part-semantic.tex", 5),
                ("part-ir.tex", 9))
    for name, expected in chapters:
        chapter_source = (parts / name).read_text(encoding="utf-8")
        sections = re.split(r"(?=\\chapter\{)", chapter_source)
        if len(sections) - 1 != expected and not args.allow_partial:
            raise SystemExit(f"{name}: expected {expected} chapters")
        text += "\n" + sections[0]
        for section in sections[1:]:
            count += 1
            if count in boundaries:
                text += "\n\\part{" + boundaries[count] + "}\n"
            text += "\n" + section
    if count != 22 and not args.allow_partial:
        raise SystemExit(f"expected 22 chapters, found {count}")
    appendix = parts / "appendices.tex"
    if appendix.is_file():
        text += "\n" + appendix.read_text(encoding="utf-8")
    text += "\n\\end{document}\n"
    output = root / "docs" / "compiler-design-tutorial.tex"
    output.write_text(text, encoding="utf-8")
    print(f"{output}: {count} chapters; {len(text):,} characters")


if __name__ == "__main__":
    main()
