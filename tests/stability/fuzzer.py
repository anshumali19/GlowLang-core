#!/usr/bin/env python3
"""
GlowLang Automated Fuzzing Infrastructure
Part of Phase 1 - Stability Hardening

This runner performs isolated fuzzing across various subsystems of the GlowLang runtime:
1. Lexer Fuzzing (raw character/binary mutation)
2. Parser Fuzzing (invalid syntax, token streams, keyword soup)
3. Tensor Fuzzing (mismatched dimensions, non-tensor arguments, extreme sizes)
4. Async/Concurrency Fuzzing (spawning invalid objects, thread exceptions, GIL stress)
5. Module/Import Fuzzing (corrupt imports, malformed standard modules)

It captures undefined behavior/crashes (SIGSEGV, SIGABRT), logs reproduction info,
and supports delta-debugging-based corpus minimization.
"""

import os
import sys
import time
import random
import subprocess
import tempfile

# Subprocess timeout in seconds
RUN_TIMEOUT = 5.0

class FuzzRunner:
    def __init__(self, binary_path="./bin/glowlang"):
        self.binary_path = binary_path
        self.crash_dir = "./tests/stability/crashes"
        os.makedirs(self.crash_dir, exist_ok=True)
        self.total_runs = 0
        self.crashes_found = 0

    def run_glow(self, code_str=None, file_path=None, stdin=None):
        """Runs GlowLang in a subprocess with resource limits."""
        self.total_runs += 1
        temp_file = None
        
        try:
            if code_str is not None:
                # Write to temp file to execute
                fd, temp_file = tempfile.mkstemp(suffix=".glow")
                with os.fdopen(fd, 'w') as f:
                    f.write(code_str)
                cmd = [self.binary_path, temp_file]
            elif file_path is not None:
                cmd = [self.binary_path, file_path]
            else:
                cmd = [self.binary_path]
                
            proc = subprocess.run(
                cmd,
                input=stdin.encode('utf-8', errors='ignore') if stdin else None,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=RUN_TIMEOUT
            )
            return proc.returncode, proc.stdout.decode('utf-8', errors='ignore'), proc.stderr.decode('utf-8', errors='ignore')
        except subprocess.TimeoutExpired:
            return -999, "", "Timeout Expired"
        finally:
            if temp_file and os.path.exists(temp_file):
                os.remove(temp_file)

    def log_crash(self, category, input_data, retcode, stdout, stderr):
        """Saves a detailed report and the minimal input required to reproduce the crash."""
        self.crashes_found += 1
        crash_id = f"crash_{category}_{int(time.time())}_{self.crashes_found}"
        report_path = os.path.join(self.crash_dir, f"{crash_id}.log")
        input_path = os.path.join(self.crash_dir, f"{crash_id}.glow")
        
        with open(input_path, 'w', errors='ignore') as f:
            f.write(input_data)
            
        with open(report_path, 'w') as f:
            f.write(f"=== GLOWLANG FUZZ CRASH REPORT ===\n")
            f.write(f"Category: {category}\n")
            f.write(f"Exit Code: {retcode}\n")
            f.write(f"Signalled: {retcode < 0 or retcode > 127}\n")
            f.write(f"Reproduction Command: {self.binary_path} {input_path}\n\n")
            f.write(f"--- STDERR ---\n{stderr}\n\n")
            f.write(f"--- STDOUT ---\n{stdout}\n")
            
        print(f"\n[CRASH] Found crash in {category}! Saved to {report_path}")
        return input_path

    def minimize_crash(self, file_path, category):
        """Performs delta debugging to minimize the crashing code segment."""
        print(f"[*] Starting minimization for {file_path}...")
        with open(file_path, 'r', errors='ignore') as f:
            code = f.read()

        lines = code.splitlines()
        if not lines:
            return

        # Simple line-by-line delta debugging
        changed = True
        while changed:
            changed = False
            for i in range(len(lines)):
                test_lines = lines[:i] + lines[i+1:]
                test_code = "\n".join(test_lines)
                
                # Check if it still crashes
                retcode, _, _ = self.run_glow(code_str=test_code)
                if retcode < 0 or retcode > 2: # Signal/Crash code
                    lines = test_lines
                    changed = True
                    break
        
        min_code = "\n".join(lines)
        min_path = file_path.replace(".glow", "_minimized.glow")
        with open(min_path, 'w') as f:
            f.write(min_code)
        print(f"[+] Minimization complete. Minimized script saved to {min_path}")

    def fuzz_lexer(self, iterations=200):
        """Fuzzes the lexer with random character streams, non-ASCII data, and boundary cases."""
        print(f"[*] Fuzzing Lexer ({iterations} iterations)...")
        charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 \n\t(){}[],.+-*/%=!<>|&;\"'\\#@$`~?\x00\xff\x80"
        
        for i in range(iterations):
            length = random.randint(10, 500)
            data = "".join(random.choice(charset) for _ in range(length))
            
            # Run via stdin
            retcode, stdout, stderr = self.run_glow(stdin=data)
            if retcode < 0 or retcode > 2:
                self.log_crash("lexer", data, retcode, stdout, stderr)
                break

    def fuzz_parser(self, iterations=200):
        """Fuzzes the parser with syntax soup, missing elements, and keyword combinations."""
        print(f"[*] Fuzzing Parser ({iterations} iterations)...")
        keywords = [
            "blueprint", "can", "init", "func", "action", "let", "when", 
            "otherwise", "end", "while", "repeat", "spawn", "return", "show",
            "=", "==", "+=", "-=", "==", "is", "is not", ":", ",", ".", "[", "]", "{", "}"
        ]
        
        for i in range(iterations):
            length = random.randint(5, 50)
            tokens = [random.choice(keywords) if random.random() < 0.7 else f"var_{random.randint(1, 10)}" for _ in range(length)]
            code = " ".join(tokens)
            
            retcode, stdout, stderr = self.run_glow(code_str=code)
            if retcode < 0 or retcode > 2:
                crash_file = self.log_crash("parser", code, retcode, stdout, stderr)
                self.minimize_crash(crash_file, "parser")
                break

    def fuzz_tensors(self, iterations=100):
        """Fuzzes the tensor API with extreme sizes, type violations, and mismatched shapes."""
        print(f"[*] Fuzzing Tensors ({iterations} iterations)...")
        
        test_templates = [
            "let t = tensor.create({size})",
            "let t = tensor.zeros({size})",
            "let t1 = tensor.zeros({size1})\nlet t2 = tensor.zeros({size2})\nlet t3 = tensor.add(t1, t2)",
            "let t1 = tensor.zeros({size1})\nlet t2 = tensor.zeros({size2})\nlet t3 = tensor.matmul(t1, {r1}, {c1}, t2, {r2}, {c2})",
            "let t = tensor.zeros(10)\nt[ {index} ] = {val}",
            "let t = tensor.zeros(10)\nlet v = t[ {index} ]",
            "let t = tensor.zeros(10)\ntranspose(t, {r1}, {c1})",
            "let t = tensor.create(none)",
            "let t = tensor.zeros(\"invalid_type\")",
            "let t = tensor.add(none, [1, 2])",
        ]
        
        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                size=random.choice([-1, 0, 1, 100000000, "none", "[]"]),
                size1=random.choice([0, 5, 1000]),
                size2=random.choice([0, 5, 2000]), # Shape mismatches
                r1=random.choice([0, 2, -5, 100]),
                c1=random.choice([0, 2, -5, 100]),
                r2=random.choice([0, 2, -5, 100]),
                c2=random.choice([0, 2, -5, 100]),
                index=random.choice([-1, 0, 10, 50, "none"]),
                val=random.choice(["1.0", "none", "[]", "{}"])
            )
            
            retcode, stdout, stderr = self.run_glow(code_str=code)
            if retcode < 0 or retcode > 2:
                crash_file = self.log_crash("tensor", code, retcode, stdout, stderr)
                self.minimize_crash(crash_file, "tensor")
                break

    def fuzz_async(self, iterations=100):
        """Fuzzes async/spawn mechanism with invalid targets and thread stresses."""
        print(f"[*] Fuzzing Async Operations ({iterations} iterations)...")
        
        test_templates = [
            "spawn {target}()",
            "func f():\n  throw {exc}\nend\nspawn f()",
            "func f(a):\n  show a\nend\nspawn f()", # Missing arg
            "spawn spawn show",
            "let i = 0\nwhile i < {count}:\n  func f():\n    let j = 0\n  end\n  spawn f()\n  i = i + 1\nend",
        ]
        
        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                target=random.choice(["none", "123", "[]", "f", "show"]),
                exc=random.choice(["\"err\"", "none", "123", "[]"]),
                count=random.choice([10, 100, 1000])
            )
            
            retcode, stdout, stderr = self.run_glow(code_str=code)
            if retcode < 0 or retcode > 2:
                crash_file = self.log_crash("async", code, retcode, stdout, stderr)
                self.minimize_crash(crash_file, "async")
                break

    def fuzz_modules(self, iterations=100):
        """Fuzzes package loading and standard modules with malformed inputs."""
        print(f"[*] Fuzzing Module Loading ({iterations} iterations)...")
        
        test_templates = [
            "use {mod}",
            "import {path}",
            "let db = sqlite.open({db_path})",
            "let db = sqlite.open(\"test.db\")\nsqlite.exec(db, {sql})",
            "let client = http.listen({port})",
        ]
        
        for _ in range(iterations):
            template = random.choice(test_templates)
            code = template.format(
                mod=random.choice(["none", "123", "[]", "invalid_module"]),
                path=random.choice(["\"/nonexistent/path.glow\"", "\"../../../bin/glowlang\"", "none"]),
                db_path=random.choice(["\"\"", "none", "123", "[]"]),
                sql=random.choice(["\"\"", "\"DROP TABLE nonexistent;\"", "\"CREATE TABLE A (a INT); SELECT FROM A;\"", "none"]),
                port=random.choice([-1, 0, 80, 99999, "none"])
            )
            
            retcode, stdout, stderr = self.run_glow(code_str=code)
            if retcode < 0 or retcode > 2:
                crash_file = self.log_crash("modules", code, retcode, stdout, stderr)
                self.minimize_crash(crash_file, "modules")
                break

def main():
    print("====================================================")
    print("          GLOWLANG STABILITY FUZZ SUITE")
    print("====================================================")
    
    runner = FuzzRunner()
    
    runner.fuzz_lexer()
    runner.fuzz_parser()
    runner.fuzz_tensors()
    runner.fuzz_async()
    runner.fuzz_modules()
    
    print("\n====================================================")
    print(f"Fuzzing completed: {runner.total_runs} total runs executed.")
    if runner.crashes_found == 0:
        print("[SUCCESS] Zero crashes found. Subsystems are industrially stable!")
    else:
        print(f"[WARNING] Fuzzing found {runner.crashes_found} crashing input(s).")
    print("====================================================")

if __name__ == "__main__":
    main()
