#!/usr/bin/env python3
"""
GlowLang Automated Fuzzing Infrastructure — Phase 1 Stability Hardening
========================================================================

Comprehensive fuzzing across all runtime subsystems:

 1. Lexer Fuzzing      — random character streams, non-ASCII, null bytes, boundary cases
 2. Parser Fuzzing     — invalid syntax, token soup, keyword combinations
 3. Tensor Fuzzing     — extreme sizes, type violations, mismatched shapes
 4. Async Fuzzing      — spawn corruption, thread chaos, scheduler stress
 5. Module Fuzzing     — corrupt imports, malformed standard modules
 6. Bytecode Fuzzing   — malformed opcode streams via code generation (NEW)
 7. Malformed Tensor Fuzzing — dimension chaos, NaN/Inf injection, cross-op corruption (NEW)
 8. Malformed Async State Fuzzing — state machine corruption, re-entrancy traps (NEW)
 9. Rope/String Fuzzing — extreme concat, embedded nulls, Unicode edge cases (NEW)
10. GC Pressure Fuzzing — allocation storms, dangling reference patterns (NEW)
11. Deep Nesting Fuzzing — extreme expression/block nesting depths (NEW)

Features:
  - Automated fuzz runner with isolated subprocess execution
  - Reproducible crash logs with full environment info
  - DDMin-based corpus minimization
  - Parallel fuzzing support
  - Structured corpus generation and mutation
"""

import os
import sys
import time
import random
import subprocess
import tempfile
import hashlib
import json
import signal
import multiprocessing
from datetime import datetime

# ═══════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════
RUN_TIMEOUT = 5.0
MAX_PARALLEL_WORKERS = 4
CRASH_SIGNAL_CODES = {-11: "SIGSEGV", -6: "SIGABRT", -7: "SIGBUS", -8: "SIGFPE", -4: "SIGILL"}


class FuzzStats:
    """Tracks comprehensive fuzzing statistics."""
    def __init__(self):
        self.total_runs = 0
        self.crashes_found = 0
        self.timeouts = 0
        self.unique_crashes = set()
        self.category_stats = {}
        self.start_time = time.time()

    def record(self, category, retcode, is_crash, is_timeout):
        self.total_runs += 1
        if category not in self.category_stats:
            self.category_stats[category] = {"runs": 0, "crashes": 0, "timeouts": 0}
        self.category_stats[category]["runs"] += 1
        if is_crash:
            self.crashes_found += 1
            self.category_stats[category]["crashes"] += 1
        if is_timeout:
            self.timeouts += 1
            self.category_stats[category]["timeouts"] += 1

    def summary(self):
        elapsed = time.time() - self.start_time
        lines = [
            f"Total runs: {self.total_runs}",
            f"Crashes found: {self.crashes_found} ({len(self.unique_crashes)} unique)",
            f"Timeouts: {self.timeouts}",
            f"Elapsed: {elapsed:.1f}s",
            f"Speed: {self.total_runs / max(elapsed, 0.1):.1f} runs/s",
            "",
            "Per-category breakdown:"
        ]
        for cat, s in sorted(self.category_stats.items()):
            lines.append(f"  {cat}: {s['runs']} runs, {s['crashes']} crashes, {s['timeouts']} timeouts")
        return "\n".join(lines)


class FuzzRunner:
    def __init__(self, binary_path="./bin/glowlang"):
        self.binary_path = os.path.abspath(binary_path)
        self.crash_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "crashes")
        self.corpus_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")
        os.makedirs(self.crash_dir, exist_ok=True)
        os.makedirs(self.corpus_dir, exist_ok=True)
        self.stats = FuzzStats()

    def run_glow(self, code_str=None, file_path=None, stdin=None):
        """Runs GlowLang in a subprocess with resource limits and isolation."""
        self.stats.total_runs  # Just for tracking, actual recording happens in callers
        temp_file = None

        try:
            if code_str is not None:
                fd, temp_file = tempfile.mkstemp(suffix=".glow", dir=self.corpus_dir)
                with os.fdopen(fd, 'w', errors='replace') as f:
                    f.write(code_str)
                cmd = [self.binary_path, temp_file]
            elif file_path is not None:
                cmd = [self.binary_path, file_path]
            else:
                cmd = [self.binary_path]

            env = os.environ.copy()
            env["GLOWLANG_GC_STRESS"] = "1"  # Enable GC stress mode if available

            proc = subprocess.run(
                cmd,
                input=stdin.encode('utf-8', errors='ignore') if stdin else None,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=RUN_TIMEOUT,
                env=env,
            )
            return proc.returncode, proc.stdout.decode('utf-8', errors='ignore'), proc.stderr.decode('utf-8', errors='ignore')
        except subprocess.TimeoutExpired:
            return -999, "", "Timeout Expired"
        except Exception as e:
            return -998, "", f"Runner exception: {str(e)}"
        finally:
            if temp_file and os.path.exists(temp_file):
                try:
                    os.remove(temp_file)
                except OSError:
                    pass

    def is_crash(self, retcode):
        """Determines if a return code indicates a real crash (signal death)."""
        return retcode < 0 and retcode != -999 and retcode != -998

    def is_interesting(self, retcode):
        """Determines if the return code is interesting (crash or unexpected exit)."""
        return retcode < 0 or retcode > 2

    def crash_fingerprint(self, stderr, retcode):
        """Generates a fingerprint for deduplication of crashes."""
        # Extract the first meaningful line from stderr
        key_lines = [l.strip() for l in stderr.splitlines() if l.strip() and "warning" not in l.lower()][:3]
        key = f"{retcode}|{'|'.join(key_lines)}"
        return hashlib.md5(key.encode()).hexdigest()[:12]

    def log_crash(self, category, input_data, retcode, stdout, stderr):
        """Saves a detailed crash report with reproduction info."""
        self.stats.crashes_found += 1
        fingerprint = self.crash_fingerprint(stderr, retcode)

        if fingerprint in self.stats.unique_crashes:
            return None  # Duplicate
        self.stats.unique_crashes.add(fingerprint)

        crash_id = f"crash_{category}_{fingerprint}_{int(time.time())}"
        report_path = os.path.join(self.crash_dir, f"{crash_id}.log")
        input_path = os.path.join(self.crash_dir, f"{crash_id}.glow")

        with open(input_path, 'w', errors='replace') as f:
            f.write(input_data)

        signal_name = CRASH_SIGNAL_CODES.get(retcode, f"Unknown ({retcode})")
        with open(report_path, 'w') as f:
            f.write(f"=== GLOWLANG FUZZ CRASH REPORT ===\n")
            f.write(f"Timestamp: {datetime.now().isoformat()}\n")
            f.write(f"Category: {category}\n")
            f.write(f"Exit Code: {retcode}\n")
            f.write(f"Signal: {signal_name}\n")
            f.write(f"Fingerprint: {fingerprint}\n")
            f.write(f"Reproduction: {self.binary_path} {input_path}\n\n")
            f.write(f"--- INPUT (first 2000 chars) ---\n{input_data[:2000]}\n\n")
            f.write(f"--- STDERR ---\n{stderr}\n\n")
            f.write(f"--- STDOUT ---\n{stdout}\n")

        print(f"\n  [CRASH] {category} | signal={signal_name} | fingerprint={fingerprint}")
        print(f"          Saved to: {report_path}")
        return input_path

    # ═══════════════════════════════════════════════════════════
    # DDMin-based Corpus Minimization
    # ═══════════════════════════════════════════════════════════
    def minimize_crash(self, file_path, category):
        """DDMin (delta debugging) minimization of crashing input."""
        print(f"  [*] Minimizing crash: {file_path}...")
        with open(file_path, 'r', errors='replace') as f:
            code = f.read()

        # Phase 1: Line-level minimization
        lines = code.splitlines()
        lines = self._ddmin(lines, lambda subset: self._test_crash("\n".join(subset)))

        # Phase 2: Character-level minimization on the result
        chars = list("\n".join(lines))
        if len(chars) <= 500:  # Only char-minimize small inputs
            chars = self._ddmin(chars, lambda subset: self._test_crash("".join(subset)))

        minimized = "".join(chars) if len(chars) <= 500 else "\n".join(lines)
        min_path = file_path.replace(".glow", "_minimized.glow")
        with open(min_path, 'w') as f:
            f.write(minimized)
        print(f"  [+] Minimized: {len(code)} -> {len(minimized)} chars -> {min_path}")
        return min_path

    def _test_crash(self, code_str):
        """Returns True if the code still crashes."""
        if not code_str.strip():
            return False
        retcode, _, _ = self.run_glow(code_str=code_str)
        return self.is_crash(retcode)

    def _ddmin(self, elements, test_fn):
        """DDMin algorithm for minimal failing subset."""
        if len(elements) <= 1:
            return elements

        n = 2
        while len(elements) >= 2:
            chunk_size = max(1, len(elements) // n)
            some_complement_fails = False

            for i in range(0, len(elements), chunk_size):
                complement = elements[:i] + elements[i + chunk_size:]
                if test_fn(complement):
                    elements = complement
                    n = max(n - 1, 2)
                    some_complement_fails = True
                    break

            if not some_complement_fails:
                if n >= len(elements):
                    break
                n = min(n * 2, len(elements))

        return elements

    # ═══════════════════════════════════════════════════════════
    # 1. Lexer Fuzzing
    # ═══════════════════════════════════════════════════════════
    def fuzz_lexer(self, iterations=200):
        """Fuzzes the lexer with random character streams, non-ASCII, null bytes, and boundary cases."""
        print(f"\n[*] Fuzzing Lexer ({iterations} iterations)...")
        charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 \n\t(){}[],.+-*/%=!<>|&;\"'\\#@$`~?\x00\xff\x80"

        for i in range(iterations):
            strategy = random.choice(["random", "binary", "unicode", "repeated", "null_heavy"])

            if strategy == "random":
                length = random.randint(10, 500)
                data = "".join(random.choice(charset) for _ in range(length))
            elif strategy == "binary":
                length = random.randint(10, 200)
                data = "".join(chr(random.randint(0, 255)) for _ in range(length))
            elif strategy == "unicode":
                length = random.randint(10, 200)
                data = "".join(chr(random.randint(0, 0xFFFF)) for _ in range(length))
            elif strategy == "repeated":
                char = random.choice(charset)
                data = char * random.randint(1000, 50000)
            else:  # null_heavy
                data = "show 1\x00" * random.randint(10, 100) + "\x00" * random.randint(10, 100)

            retcode, stdout, stderr = self.run_glow(code_str=data)
            is_crash = self.is_crash(retcode)
            self.stats.record("lexer", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("lexer", data, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "lexer")

    # ═══════════════════════════════════════════════════════════
    # 2. Parser Fuzzing
    # ═══════════════════════════════════════════════════════════
    def fuzz_parser(self, iterations=200):
        """Fuzzes the parser with syntax soup, missing elements, and keyword combinations."""
        print(f"\n[*] Fuzzing Parser ({iterations} iterations)...")
        keywords = [
            "blueprint", "can", "init", "func", "action", "let", "when",
            "otherwise", "end", "while", "repeat", "spawn", "return", "show",
            "=", "==", "+=", "-=", "==", "is", "is not", ":", ",", ".", "[", "]", "{", "}",
            "if", "for", "each", "in", "try", "catch", "throw", "class", "self",
            "lambda", "import", "use", "true", "false", "none", "and", "or", "not"
        ]

        for i in range(iterations):
            strategy = random.choice(["keyword_soup", "partial_structure", "deep_nesting", "unclosed"])

            if strategy == "keyword_soup":
                length = random.randint(5, 50)
                tokens = [random.choice(keywords) if random.random() < 0.7 else f"var_{random.randint(1, 10)}" for _ in range(length)]
                code = " ".join(tokens)
            elif strategy == "partial_structure":
                templates = [
                    "func (:\n  show\nend",
                    "class :\n  func ():\n  end\nend",
                    "if :\n  show\n",  # missing end
                    "while :\n  \nend",
                    "for each in :\nend",
                    "try:\n  throw\ncatch :\nend",
                    "let = \nlet = = =",
                ]
                code = random.choice(templates)
            elif strategy == "deep_nesting":
                depth = random.randint(20, 100)
                code = "if true:\n" * depth + "  show 1\n" + "end\n" * depth
            else:  # unclosed
                code = random.choice([
                    "func f():\n  show 1\n",
                    "class C:\n  func f():\n  end\n",
                    "try:\n  show 1\n",
                    "[1, 2, 3",
                    "{\"a\": 1, \"b\":",
                    "\"unterminated string",
                ])

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("parser", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("parser", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "parser")

    # ═══════════════════════════════════════════════════════════
    # 3. Tensor Fuzzing
    # ═══════════════════════════════════════════════════════════
    def fuzz_tensors(self, iterations=100):
        """Fuzzes the tensor API with extreme sizes, type violations, and mismatched shapes."""
        print(f"\n[*] Fuzzing Tensors ({iterations} iterations)...")

        test_templates = [
            "let t = tensor.create({size})",
            "let t = tensor.zeros({size})",
            "let t1 = tensor.zeros({size1})\nlet t2 = tensor.zeros({size2})\nlet t3 = tensor.add(t1, t2)",
            "let t1 = tensor.zeros({size1})\nlet t2 = tensor.zeros({size2})\nlet t3 = tensor.matmul(t1, {r1}, {c1}, t2, {r2}, {c2})",
            "let t = tensor.zeros(10)\nt[ {index} ] = {val}",
            "let t = tensor.zeros(10)\nlet v = t[ {index} ]",
            "let t = tensor.zeros(10)\ntranspose(t, {r1}, {c1})",
            "let t = tensor.create(none)",
            'let t = tensor.zeros("invalid_type")',
            "let t = tensor.add(none, [1, 2])",
        ]

        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                size=random.choice([-1, 0, 1, 100000000, "none", "[]"]),
                size1=random.choice([0, 5, 1000]),
                size2=random.choice([0, 5, 2000]),
                r1=random.choice([0, 2, -5, 100]),
                c1=random.choice([0, 2, -5, 100]),
                r2=random.choice([0, 2, -5, 100]),
                c2=random.choice([0, 2, -5, 100]),
                index=random.choice([-1, 0, 10, 50, "none"]),
                val=random.choice(["1.0", "none", "[]", "{}"])
            )

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("tensor", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("tensor", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "tensor")

    # ═══════════════════════════════════════════════════════════
    # 4. Async Fuzzing
    # ═══════════════════════════════════════════════════════════
    def fuzz_async(self, iterations=100):
        """Fuzzes async/spawn mechanism with invalid targets and thread stress."""
        print(f"\n[*] Fuzzing Async Operations ({iterations} iterations)...")

        test_templates = [
            "spawn {target}()",
            'func f():\n  throw {exc}\nend\nspawn f()',
            'func f(a):\n  show a\nend\nspawn f()',
            "spawn spawn show",
            'let i = 0\nwhile i < {count}:\n  func f():\n    let j = 0\n  end\n  spawn f()\n  i = i + 1\nend',
        ]

        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                target=random.choice(["none", "123", "[]", "f", "show"]),
                exc=random.choice(['"err"', "none", "123", "[]"]),
                count=random.choice([10, 100, 1000])
            )

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("async", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("async", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "async")

    # ═══════════════════════════════════════════════════════════
    # 5. Module Fuzzing
    # ═══════════════════════════════════════════════════════════
    def fuzz_modules(self, iterations=100):
        """Fuzzes package loading and standard modules with malformed inputs."""
        print(f"\n[*] Fuzzing Module Loading ({iterations} iterations)...")

        test_templates = [
            "use {mod}",
            "import {path}",
            "let db = sqlite.open({db_path})",
            'let db = sqlite.open("test.db")\nsqlite.exec(db, {sql})',
            "let client = http.listen({port})",
        ]

        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                mod=random.choice(["none", "123", "[]", "invalid_module"]),
                path=random.choice(['"/nonexistent/path.glow"', '"../../../bin/glowlang"', "none"]),
                db_path=random.choice(['""', "none", "123", "[]"]),
                sql=random.choice(['""', '"DROP TABLE nonexistent;"', '"CREATE TABLE A (a INT); SELECT FROM A;"', "none"]),
                port=random.choice([-1, 0, 80, 99999, "none"])
            )

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("modules", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("modules", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "modules")

    # ═══════════════════════════════════════════════════════════
    # 6. Bytecode Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_bytecode(self, iterations=150):
        """Generates code patterns that stress bytecode compilation edge cases.

        This fuzzes the compiler's bytecode generation by creating programs
        that exercise unusual opcode sequences, extreme jump targets,
        register pressure, and malformed control flow graphs.
        """
        print(f"\n[*] Fuzzing Bytecode Generation ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "extreme_locals", "deep_control_flow", "register_pressure",
                "dead_code_storm", "exception_chaos", "lambda_bomb",
                "compound_assign_chaos", "mixed_ops"
            ])

            if strategy == "extreme_locals":
                # Create many local variables to stress register allocation
                num_vars = random.randint(50, 500)
                lines = []
                for j in range(num_vars):
                    lines.append(f"let v{j} = {j}")
                # Reference some randomly
                for _ in range(20):
                    v = random.randint(0, num_vars - 1)
                    lines.append(f"show v{v}")
                code = "\n".join(lines)

            elif strategy == "deep_control_flow":
                # Deeply nested if/while/for
                depth = random.randint(10, 50)
                code = ""
                for d in range(depth):
                    indent = "  " * d
                    choice = random.choice(["if", "while"])
                    if choice == "if":
                        code += f"{indent}if true:\n"
                    else:
                        code += f"{indent}let c{d} = 0\n{indent}while c{d} < 1:\n{indent}  c{d} = c{d} + 1\n"
                code += "  " * depth + "show 1\n"
                for d in range(depth - 1, -1, -1):
                    code += "  " * d + "end\n"

            elif strategy == "register_pressure":
                # Chain of operations to stress register allocation
                code = "let a = 1\n"
                for j in range(random.randint(20, 100)):
                    op = random.choice(["+", "-", "*"])
                    code += f"let b{j} = a {op} {j + 1}\n"
                    code += f"a = b{j}\n"
                code += "show a\n"

            elif strategy == "dead_code_storm":
                # Functions that are defined but never called, interleaved with live code
                lines = []
                for j in range(random.randint(10, 50)):
                    lines.append(f"func dead_{j}():\n  return {j}\nend\n")
                lines.append("show 42\n")
                code = "\n".join(lines)

            elif strategy == "exception_chaos":
                # Deeply nested try/catch with throws at various levels
                depth = random.randint(3, 15)
                code = ""
                for d in range(depth):
                    indent = "  " * d
                    code += f"{indent}try:\n"
                    if random.random() < 0.3:
                        code += f"{indent}  throw \"{d}\"\n"
                    else:
                        code += f"{indent}  let x{d} = {d}\n"
                code += "  " * depth + "show 1\n"
                for d in range(depth - 1, -1, -1):
                    indent = "  " * d
                    code += f"{indent}catch e{d}:\n{indent}  show e{d}\n{indent}end\n"

            elif strategy == "lambda_bomb":
                # Many lambdas capturing various scopes
                lines = ["let base = 100"]
                for j in range(random.randint(5, 30)):
                    lines.append(f"let fn{j} = lambda(x): x + base + {j}")
                for j in range(min(5, random.randint(1, 30))):
                    lines.append(f"show fn{j}(1)")
                code = "\n".join(lines)

            elif strategy == "compound_assign_chaos":
                # Compound assignment with various types
                code = "let x = 0\n"
                for _ in range(random.randint(20, 100)):
                    op = random.choice(["+=", "-="])
                    val = random.choice(["1", "0", "x", "none"])
                    code += f"x {op} {val}\n"
                code += "show x\n"

            else:  # mixed_ops
                # Random mix of all operation types
                ops = ["let x = 1", "show x", "x = x + 1", "x = x - 1",
                       "x = x * 2", "x = x / 2", "x = x % 3",
                       "let l = [1, 2, 3]", "show len(l)", "l = l + [4]"]
                random.shuffle(ops)
                code = "\n".join(ops[:random.randint(5, len(ops))])

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("bytecode", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("bytecode", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "bytecode")

    # ═══════════════════════════════════════════════════════════
    # 7. Malformed Tensor Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_malformed_tensors(self, iterations=100):
        """Dimension chaos, NaN/Inf injection, cross-operation type confusion."""
        print(f"\n[*] Fuzzing Malformed Tensors ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "nan_inf_inject", "cross_op_type", "zero_dim_ops",
                "negative_size", "double_free_pattern", "index_boundary"
            ])

            if strategy == "nan_inf_inject":
                code = """let t = tensor.zeros(10)
t[0] = 0.0 / 0.0
t[1] = 1.0 / 0.0
t[2] = -1.0 / 0.0
let t2 = tensor.zeros(10)
let t3 = tensor.add(t, t2)
let d = tensor.dot(t, t2)
show d
"""
            elif strategy == "cross_op_type":
                # Try tensor ops on non-tensors
                targets = random.choice([
                    'let t = [1, 2, 3]\nlet r = tensor.add(t, t)',
                    'let t = "hello"\nlet r = tensor.dot(t, t)',
                    'let t = 42\nlet r = tensor.mul(t, t)',
                    'let t = none\nlet r = tensor.zeros(t)',
                    'let t = true\nlet r = tensor.add(t, false)',
                ])
                code = targets

            elif strategy == "zero_dim_ops":
                code = """let t1 = tensor.zeros(0)
let t2 = tensor.zeros(0)
let t3 = tensor.add(t1, t2)
let d = tensor.dot(t1, t2)
show t3
show d
"""
            elif strategy == "negative_size":
                size = random.choice([-1, -100, -999999])
                code = f"""let t = tensor.zeros({size})
show t
"""
            elif strategy == "double_free_pattern":
                # Allocate tensor, overwrite reference, then use old name
                code = """let t1 = tensor.zeros(100)
let t2 = tensor.zeros(100)
t1 = t2
let t3 = tensor.add(t1, t2)
t2 = tensor.zeros(50)
let t4 = tensor.add(t1, t2)
show t4
"""
            else:  # index_boundary
                idx = random.choice([-1, -100, 10, 100, 1000000, "none"])
                code = f"""let t = tensor.zeros(10)
t[{idx}] = 42.0
show t[{idx}]
"""

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("malformed_tensor", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("malformed_tensor", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "malformed_tensor")

    # ═══════════════════════════════════════════════════════════
    # 8. Malformed Async State Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_malformed_async(self, iterations=100):
        """State machine corruption, re-entrancy traps, spawn-in-spawn."""
        print(f"\n[*] Fuzzing Malformed Async State ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "spawn_in_spawn", "spawn_throwing", "spawn_with_closure",
                "rapid_spawn_exit", "spawn_recursive", "spawn_shared_state"
            ])

            if strategy == "spawn_in_spawn":
                depth = random.randint(2, 5)
                code = ""
                for d in range(depth):
                    fname = f"nested_{d}"
                    inner = f"nested_{d + 1}" if d < depth - 1 else None
                    code += f"func {fname}():\n"
                    if inner:
                        code += f"  spawn {inner}()\n"
                    else:
                        code += f"  show \"deepest\"\n"
                    code += "end\n\n"
                code += f"spawn nested_0()\nos.sleep(500)\n"

            elif strategy == "spawn_throwing":
                code = """func thrower():
  let i = 0
  while i < 10:
    throw "async error"
    i = i + 1
  end
end

let j = 0
while j < 20:
  try:
    spawn thrower()
  catch e:
    show "caught: ", e
  end
  j = j + 1
end
os.sleep(200)
"""
            elif strategy == "spawn_with_closure":
                code = """func make_task(id):
  let captured_id = id
  return lambda(): show "task: " + captured_id
end

let k = 0
while k < 50:
  let task_fn = make_task(k)
  spawn task_fn()
  k = k + 1
end
os.sleep(300)
"""
            elif strategy == "rapid_spawn_exit":
                count = random.randint(100, 500)
                code = f"""func noop():
  return none
end

let i = 0
while i < {count}:
  spawn noop()
  i = i + 1
end
show "spawned all"
"""
            elif strategy == "spawn_recursive":
                code = """func recurse_spawn(n):
  if n <= 0:
    return 0
  end
  show n
  spawn recurse_spawn(n - 1)
end

spawn recurse_spawn(10)
os.sleep(500)
"""
            else:  # spawn_shared_state
                code = """let counter = 0

func increment():
  let i = 0
  while i < 100:
    counter = counter + 1
    i = i + 1
  end
end

let s = 0
while s < 10:
  spawn increment()
  s = s + 1
end
os.sleep(500)
show "counter: ", counter
"""

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("malformed_async", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("malformed_async", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "malformed_async")

    # ═══════════════════════════════════════════════════════════
    # 9. Rope/String Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_ropes(self, iterations=100):
        """Extreme string concatenation, embedded nulls, Unicode edge cases."""
        print(f"\n[*] Fuzzing Rope/String Operations ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "huge_concat", "embedded_null", "deep_rope_tree",
                "slice_boundary", "fstring_chaos", "builder_stress"
            ])

            if strategy == "huge_concat":
                count = random.choice([1000, 5000, 10000])
                code = f"""let s = ""
let i = 0
while i < {count}:
  s = s + "A"
  i = i + 1
end
show len(s)
"""
            elif strategy == "embedded_null":
                code = 'let s = "hello\\x00world"\nshow len(s)\nshow s\n'

            elif strategy == "deep_rope_tree":
                # Build a deep rope tree by alternating concat sides
                code = 'let s = "start"\n'
                for j in range(random.randint(50, 200)):
                    if random.random() < 0.5:
                        code += f's = s + "x{j}"\n'
                    else:
                        code += f's = "y{j}" + s\n'
                code += "show len(s)\n"

            elif strategy == "slice_boundary":
                code = """let s = "Hello World Test String"
show s[0:0]
show s[-1:100]
show s[100:200]
show s[5:3]
show s[-100:-200]
"""
            elif strategy == "fstring_chaos":
                code = """let x = 42
let y = "hello"
let z = [1, 2, 3]
show f"value: {x} str: {y} list: {z}"
show f"{x + y}"
show f"{'nested' + 'fstring'}"
show f""
"""
            else:  # builder_stress
                count = random.choice([1000, 10000, 50000])
                code = f"""let sb = text_builder()
let i = 0
while i < {count}:
  append(sb, "X")
  i = i + 1
end
let result = build(sb)
show len(result)
"""

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("rope_string", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("rope_string", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "rope_string")

    # ═══════════════════════════════════════════════════════════
    # 10. GC Pressure Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_gc_pressure(self, iterations=80):
        """Allocation storms and dangling reference patterns to stress the garbage collector."""
        print(f"\n[*] Fuzzing GC Pressure ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "allocation_storm", "circular_reference", "closure_capture_gc",
                "list_of_lists", "dict_storm", "interleaved_alloc_free"
            ])

            if strategy == "allocation_storm":
                count = random.choice([5000, 10000, 50000])
                code = f"""let i = 0
while i < {count}:
  let s = "allocation_" + i
  let l = [i, i + 1, i + 2]
  i = i + 1
end
show "survived allocation storm"
"""
            elif strategy == "circular_reference":
                code = """class Node:
  func init(val):
    self.val = val
    self.next = none
  end
end

let a = Node(1)
let b = Node(2)
let c = Node(3)
a.next = b
b.next = c
c.next = a
a = none
b = none
c = none
show "circular refs cleared"
"""
            elif strategy == "closure_capture_gc":
                code = """func make_closures():
  let closures = []
  let i = 0
  while i < 100:
    let captured = "value_" + i
    let f = lambda(): captured
    closures = closures + [f]
    i = i + 1
  end
  return closures
end

let fns = make_closures()
show len(fns)
fns = []
show "closures released"
"""
            elif strategy == "list_of_lists":
                code = """let outer = []
let i = 0
while i < 500:
  let inner = []
  let j = 0
  while j < 50:
    inner = inner + [j]
    j = j + 1
  end
  outer = outer + [inner]
  if i % 100 == 0:
    outer = []
  end
  i = i + 1
end
show "list-of-lists survived"
"""
            elif strategy == "dict_storm":
                code = """let i = 0
while i < 1000:
  let d = {"key": i, "val": "str_" + i}
  i = i + 1
end
show "dict storm survived"
"""
            else:  # interleaved_alloc_free
                code = """let refs = []
let i = 0
while i < 2000:
  refs = refs + ["item_" + i]
  if i % 10 == 0:
    refs = []
  end
  i = i + 1
end
show "interleaved alloc survived"
"""

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("gc_pressure", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("gc_pressure", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "gc_pressure")

    # ═══════════════════════════════════════════════════════════
    # 11. Deep Nesting Fuzzing (NEW)
    # ═══════════════════════════════════════════════════════════
    def fuzz_deep_nesting(self, iterations=50):
        """Extreme expression and block nesting depths."""
        print(f"\n[*] Fuzzing Deep Nesting ({iterations} iterations)...")

        for _ in range(iterations):
            strategy = random.choice([
                "nested_parens", "nested_lists", "nested_calls",
                "nested_lambdas", "nested_ternary_sim"
            ])

            if strategy == "nested_parens":
                depth = random.randint(50, 500)
                code = f"show {'(' * depth}1{')' * depth}\n"

            elif strategy == "nested_lists":
                depth = random.randint(10, 100)
                inner = "1"
                for _ in range(depth):
                    inner = f"[{inner}]"
                code = f"let x = {inner}\nshow x\n"

            elif strategy == "nested_calls":
                code = "func id(x):\n  return x\nend\n"
                call = "1"
                for _ in range(random.randint(10, 100)):
                    call = f"id({call})"
                code += f"show {call}\n"

            elif strategy == "nested_lambdas":
                depth = random.randint(5, 30)
                code = "let f = "
                for d in range(depth):
                    code += f"lambda(x{d}): "
                code += "1"
                code += "\n"
                # Call the chain
                call = "f"
                for d in range(depth):
                    call = f"{call}({d})"
                code += f"show {call}\n"

            else:  # nested_ternary_sim (simulated with if/else chains)
                depth = random.randint(10, 50)
                code = f"let x = 0\n"
                for d in range(depth):
                    code += "  " * d + f"if x == {d}:\n"
                    code += "  " * (d + 1) + f"x = {d + 1}\n"
                for d in range(depth - 1, -1, -1):
                    code += "  " * d + "end\n"
                code += "show x\n"

            retcode, stdout, stderr = self.run_glow(code_str=code)
            is_crash = self.is_crash(retcode)
            self.stats.record("deep_nesting", retcode, is_crash, retcode == -999)
            if is_crash:
                crash_file = self.log_crash("deep_nesting", code, retcode, stdout, stderr)
                if crash_file:
                    self.minimize_crash(crash_file, "deep_nesting")


# ═══════════════════════════════════════════════════════════════
# Main Entry Point
# ═══════════════════════════════════════════════════════════════
def main():
    print("=" * 60)
    print("       GLOWLANG STABILITY FUZZ SUITE — Phase 1")
    print("=" * 60)
    print(f"Started: {datetime.now().isoformat()}")
    print()

    binary = "./bin/glowlang"
    if len(sys.argv) > 1:
        binary = sys.argv[1]

    runner = FuzzRunner(binary)

    # Original categories
    runner.fuzz_lexer()
    runner.fuzz_parser()
    runner.fuzz_tensors()
    runner.fuzz_async()
    runner.fuzz_modules()

    # New Phase 1 categories
    runner.fuzz_bytecode()
    runner.fuzz_malformed_tensors()
    runner.fuzz_malformed_async()
    runner.fuzz_ropes()
    runner.fuzz_gc_pressure()
    runner.fuzz_deep_nesting()

    print()
    print("=" * 60)
    print("       FUZZ SUITE RESULTS")
    print("=" * 60)
    print(runner.stats.summary())
    print()

    if runner.stats.crashes_found == 0:
        print("[SUCCESS] Zero crashes found across all subsystems!")
    else:
        print(f"[WARNING] Found {runner.stats.crashes_found} crash(es) ({len(runner.stats.unique_crashes)} unique).")
        print(f"          Crash reports saved to: {runner.crash_dir}")

    print("=" * 60)

    # Write machine-readable summary
    summary_path = os.path.join(runner.crash_dir, "fuzz_summary.json")
    with open(summary_path, 'w') as f:
        json.dump({
            "timestamp": datetime.now().isoformat(),
            "total_runs": runner.stats.total_runs,
            "crashes": runner.stats.crashes_found,
            "unique_crashes": len(runner.stats.unique_crashes),
            "timeouts": runner.stats.timeouts,
            "categories": runner.stats.category_stats,
        }, f, indent=2)

    return 0 if runner.stats.crashes_found == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
