from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPILER = Path(sys.argv.pop(1)).resolve()
CLANG = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None


class LLVMCLI(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="rustless-llvm-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def compile(self, text, *options):
        source = self.root / "program.rx"
        source.write_text(text, encoding="utf-8")
        output = self.root / "program.ll"
        result = subprocess.run(
            [str(COMPILER), "--stage", "ir", str(source), "-o", str(output), *options],
            capture_output=True, text=True, timeout=20)
        return result, output

    def test_ir_and_llvm_compilation(self):
        result, output = self.compile("""
#[derive(Clone, PartialEq, Eq)]
struct Packet { flag: bool, values: [i32; 2] }
fn main() {
  let mut values = Vec::<Packet>::new();
  values.push(Packet { values: [1, 2], flag: true });
  let copied = values.clone();
  let same = copied == values;
  let item = values.remove(0);
  if same { printlnInt(item.values[1]); }
}
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        llvm = output.read_text(encoding="utf-8")
        self.assertIn("define i32 @main()", llvm)
        self.assertIn("define void @printlnInt", llvm)
        self.assertIn("phi i32", llvm)
        if CLANG:
            assembly = self.root / "program.s"
            lowered = subprocess.run(
                [str(CLANG), "-S", "-x", "ir", str(output), "-o", str(assembly),
                 "--target=riscv32-unknown-elf", "-march=rv32im", "-mabi=ilp32", "-O0"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(lowered.returncode, 0, lowered.stderr)
            self.assertTrue(assembly.is_file())

    def test_rejected_program_has_no_ir(self):
        result, output = self.compile("fn main() { let value: i32 = true; }")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(output.exists())

    def test_ast_dump_cannot_mix_with_ir(self):
        result, output = self.compile("fn main() {}", "--dump-ast")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
