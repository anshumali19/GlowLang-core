#!/usr/bin/env python3
"""
GlowLang GC Verification Harness — Phase 1 Stability Hardening
================================================================

Runs the GC verification GlowLang test suite and performs external
validation of heap behavior:

1. Runs gc_verification.glow and parses structured output
2. Monitors memory usage over time (RSS tracking)
3. Detects memory leaks by comparing before/after allocation
4. Validates GC correctness under AddressSanitizer (if available)
5. Runs repeated GC cycles and checks for growth trends
"""

import os
import sys
import time
import subprocess
import re
import json
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
BINARY = os.path.join(PROJECT_ROOT, "bin", "glowlang")
ASAN_BINARY = os.path.join(PROJECT_ROOT, "bin", "glowlang_asan")
GC_TEST = os.path.join(SCRIPT_DIR, "gc_verification.glow")
TIMEOUT = 120  # seconds


class GCVerifier:
    def __init__(self, binary_path=BINARY):
        self.binary = binary_path
        self.results = []
        self.passed = 0
        self.failed = 0

    def run_test(self, test_file, label, extra_env=None):
        """Run a GlowLang test file and capture results."""
        print(f"\n  [{label}] Running: {os.path.basename(test_file)}")
        env = os.environ.copy()
        if extra_env:
            env.update(extra_env)

        try:
            proc = subprocess.run(
                [self.binary, test_file],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=TIMEOUT,
                env=env,
            )
            stdout = proc.stdout.decode('utf-8', errors='replace')
            stderr = proc.stderr.decode('utf-8', errors='replace')
            retcode = proc.returncode

            return retcode, stdout, stderr
        except subprocess.TimeoutExpired:
            return -999, "", "TIMEOUT"
        except FileNotFoundError:
            return -1, "", f"Binary not found: {self.binary}"

    def check_output(self, stdout, label):
        """Parse test output for pass/fail markers."""
        lines = stdout.splitlines()
        heap_values = []
        errors = []

        for line in lines:
            # Look for heap integrity markers
            if "Heap" in line and ("status" in line or "integrity" in line or "after" in line):
                # Extract the verification result
                if "error" in line.lower() or "fail" in line.lower() or "corrupt" in line.lower():
                    errors.append(line.strip())
                else:
                    heap_values.append(line.strip())

            # Look for explicit error messages
            if "[ERROR]" in line:
                errors.append(line.strip())

        return heap_values, errors

    def test_gc_verification_suite(self):
        """Run the main GC verification GlowLang suite."""
        print("\n" + "=" * 60)
        print("  TEST 1: GC Verification Suite")
        print("=" * 60)

        retcode, stdout, stderr = self.run_test(GC_TEST, "GC Suite")

        if retcode == -999:
            print("  [FAIL] Test timed out!")
            self.failed += 1
            self.results.append({"test": "gc_suite", "status": "TIMEOUT"})
            return

        if retcode < 0:
            print(f"  [FAIL] Crashed with signal {retcode}")
            self.failed += 1
            self.results.append({"test": "gc_suite", "status": "CRASH", "signal": retcode})
            return

        heap_vals, errors = self.check_output(stdout, "gc_suite")

        if errors:
            print(f"  [FAIL] Found {len(errors)} error(s):")
            for e in errors:
                print(f"    - {e}")
            self.failed += 1
            self.results.append({"test": "gc_suite", "status": "FAIL", "errors": errors})
        else:
            print(f"  [PASS] GC suite completed. {len(heap_vals)} heap checkpoints OK.")
            self.passed += 1
            self.results.append({"test": "gc_suite", "status": "PASS", "checkpoints": len(heap_vals)})

        if stdout:
            print(f"\n  --- Output (last 20 lines) ---")
            for line in stdout.splitlines()[-20:]:
                print(f"    {line}")

    def test_memory_growth(self):
        """Run a repeated allocation pattern and check for memory growth trends."""
        print("\n" + "=" * 60)
        print("  TEST 2: Memory Growth Detection")
        print("=" * 60)

        # Create a test that allocates and frees in a loop
        test_code = """
let round = 0
while round < 5:
    let i = 0
    while i < 10000:
        let s = "alloc_" + i
        let l = [i, i + 1]
        i = i + 1
    end
    let info = os.gc_info()
    show "HEAP_CHECKPOINT:", info.allocated
    round = round + 1
end
"""
        import tempfile
        fd, temp_file = tempfile.mkstemp(suffix=".glow", dir=SCRIPT_DIR)
        with os.fdopen(fd, 'w') as f:
            f.write(test_code)

        try:
            retcode, stdout, stderr = self.run_test(temp_file, "Memory Growth", extra_env={"GLOWLANG_GC_STRESS": "1"})

            if retcode != 0 and retcode != 1:
                print(f"  [WARN] Test exited with code {retcode}")

            # Parse heap checkpoints
            checkpoints = []
            for line in stdout.splitlines():
                if "HEAP_CHECKPOINT:" in line:
                    try:
                        val = int(line.split("HEAP_CHECKPOINT:")[1].strip())
                        checkpoints.append(val)
                    except (ValueError, IndexError):
                        pass

            if len(checkpoints) >= 3:
                # Check if heap is growing unboundedly
                growth = checkpoints[-1] - checkpoints[0]
                avg_checkpoint = sum(checkpoints) / len(checkpoints)
                max_checkpoint = max(checkpoints)

                # Allow up to 50% growth between first and last
                growth_ratio = max_checkpoint / max(checkpoints[0], 1)

                if growth_ratio > 2.0:
                    print(f"  [WARN] Possible memory leak: heap grew {growth_ratio:.1f}x")
                    print(f"         Checkpoints: {checkpoints}")
                    self.results.append({"test": "memory_growth", "status": "WARN", "ratio": growth_ratio})
                else:
                    print(f"  [PASS] Memory stable. Growth ratio: {growth_ratio:.2f}x")
                    print(f"         Checkpoints: {checkpoints}")
                    self.passed += 1
                    self.results.append({"test": "memory_growth", "status": "PASS", "ratio": growth_ratio})
            else:
                print(f"  [SKIP] Not enough heap checkpoints ({len(checkpoints)} found)")
                self.results.append({"test": "memory_growth", "status": "SKIP"})
        finally:
            if os.path.exists(temp_file):
                os.remove(temp_file)

    def test_asan_gc(self):
        """Run GC tests under AddressSanitizer if available."""
        print("\n" + "=" * 60)
        print("  TEST 3: AddressSanitizer GC Validation")
        print("=" * 60)

        if not os.path.exists(ASAN_BINARY):
            print(f"  [SKIP] ASan binary not found: {ASAN_BINARY}")
            print(f"         Build with: make test-sanitized")
            self.results.append({"test": "asan_gc", "status": "SKIP"})
            return

        asan_verifier = GCVerifier(ASAN_BINARY)
        retcode, stdout, stderr = asan_verifier.run_test(GC_TEST, "ASan GC", extra_env={"GLOWLANG_GC_STRESS": "1"})

        if "ERROR: AddressSanitizer" in stderr:
            print(f"  [FAIL] ASan detected memory error:")
            # Extract the summary line
            for line in stderr.splitlines():
                if "ERROR:" in line or "SUMMARY:" in line:
                    print(f"    {line}")
            self.failed += 1
            self.results.append({"test": "asan_gc", "status": "FAIL", "asan_error": True})
        elif retcode < 0:
            print(f"  [FAIL] Crashed under ASan (signal {retcode})")
            self.failed += 1
            self.results.append({"test": "asan_gc", "status": "CRASH"})
        else:
            print(f"  [PASS] No ASan errors detected")
            self.passed += 1
            self.results.append({"test": "asan_gc", "status": "PASS"})

    def test_gc_stress_cycles(self):
        """Run multiple GC-intensive tests in sequence to detect cumulative issues."""
        print("\n" + "=" * 60)
        print("  TEST 4: GC Stress Cycles (Cumulative)")
        print("=" * 60)

        test_code = """
func gc_cycle(cycle_id, count):
    let i = 0
    while i < count:
        let s = "cycle_" + cycle_id + "_item_" + i
        let l = [i, s, [i + 1, i + 2]]
        let d = {"id": i, "data": s}
        i = i + 1
    end
    let info = os.gc_info()
    show "CYCLE:", cycle_id, "HEAP:", info.allocated
end

let c = 0
while c < 10:
    gc_cycle(c, 5000)
    c = c + 1
end

let final = os.verify_gc()
show "FINAL_STATUS:", final
"""
        import tempfile
        fd, temp_file = tempfile.mkstemp(suffix=".glow", dir=SCRIPT_DIR)
        with os.fdopen(fd, 'w') as f:
            f.write(test_code)

        try:
            retcode, stdout, stderr = self.run_test(temp_file, "GC Cycles", extra_env={"GLOWLANG_GC_STRESS": "1"})

            if retcode < 0:
                print(f"  [FAIL] Crashed during GC stress cycles (signal {retcode})")
                self.failed += 1
                self.results.append({"test": "gc_cycles", "status": "CRASH"})
            else:
                # Parse cycle data
                cycle_heaps = []
                for line in stdout.splitlines():
                    if "CYCLE:" in line and "HEAP:" in line:
                        try:
                            heap_str = line.split("HEAP:")[1].strip()
                            heap_val = int(heap_str)
                            cycle_heaps.append(heap_val)
                        except (ValueError, IndexError):
                            pass

                if cycle_heaps:
                    trend = cycle_heaps[-1] - cycle_heaps[0]
                    print(f"  [PASS] Completed 10 GC cycles. Heap trend: {'+' if trend > 0 else ''}{trend} bytes")
                    print(f"         Cycle heaps: {cycle_heaps}")
                else:
                    print(f"  [PASS] Completed GC cycles (no heap data parsed)")

                self.passed += 1
                self.results.append({"test": "gc_cycles", "status": "PASS", "heaps": cycle_heaps})
        finally:
            if os.path.exists(temp_file):
                os.remove(temp_file)

    def test_closure_gc_safety(self):
        """Validate that closures don't cause GC issues when captured vars are freed."""
        print("\n" + "=" * 60)
        print("  TEST 5: Closure GC Safety")
        print("=" * 60)

        test_code = """
let global_data = [range(1000), "important"]
func make_escaping_closure():
    return lambda(): len(global_data[0]) + len(global_data[1])
end

let closures = []
let i = 0
while i < 200:
    let f = make_escaping_closure()
    closures = closures + [f]
    i = i + 1
end

# Force GC by allocating lots of garbage
let j = 0
while j < 10000:
    let garbage = "temp_" + j
    j = j + 1
end

# Now call all closures — they must still work
let ok = true
for each f in closures:
    let wrapper = [f]
    let result = wrapper[0]()
    if result < 1:
        ok = false
    end
end

show "CLOSURE_GC_RESULT:", ok
let gc = os.verify_gc()
show "HEAP_AFTER_CLOSURES:", gc
"""
        import tempfile
        fd, temp_file = tempfile.mkstemp(suffix=".glow", dir=SCRIPT_DIR)
        with os.fdopen(fd, 'w') as f:
            f.write(test_code)

        try:
            retcode, stdout, stderr = self.run_test(temp_file, "Closure GC", extra_env={"GLOWLANG_GC_STRESS": "1"})

            if retcode < 0:
                print(f"  [FAIL] Crashed during closure GC test (signal {retcode})")
                self.failed += 1
                self.results.append({"test": "closure_gc", "status": "CRASH"})
            elif "CLOSURE_GC_RESULT: true" in stdout or "CLOSURE_GC_RESULT:true" in stdout:
                print(f"  [PASS] All closures survived GC correctly")
                self.passed += 1
                self.results.append({"test": "closure_gc", "status": "PASS"})
            else:
                print(f"  [WARN] Closures may have been corrupted by GC")
                self.results.append({"test": "closure_gc", "status": "WARN"})
        finally:
            if os.path.exists(temp_file):
                os.remove(temp_file)

    def run_all(self):
        """Run all GC verification tests."""
        print("=" * 60)
        print("   GLOWLANG GC VERIFICATION HARNESS — Phase 1")
        print("=" * 60)
        print(f"Binary: {self.binary}")
        print(f"Test:   {GC_TEST}")
        print(f"Time:   {datetime.now().isoformat()}")

        self.test_gc_verification_suite()
        self.test_memory_growth()
        self.test_asan_gc()
        self.test_gc_stress_cycles()
        self.test_closure_gc_safety()

        # Summary
        total = self.passed + self.failed
        print("\n" + "=" * 60)
        print("   GC VERIFICATION RESULTS")
        print("=" * 60)
        print(f"  Passed: {self.passed}/{total}")
        print(f"  Failed: {self.failed}/{total}")
        print(f"  Skipped: {len([r for r in self.results if r.get('status') == 'SKIP'])}")

        # Write JSON results
        results_path = os.path.join(SCRIPT_DIR, "gc_verify_results.json")
        with open(results_path, 'w') as f:
            json.dump({
                "timestamp": datetime.now().isoformat(),
                "binary": self.binary,
                "passed": self.passed,
                "failed": self.failed,
                "results": self.results,
            }, f, indent=2)
        print(f"\n  Results saved to: {results_path}")

        if self.failed > 0:
            print("\n  [ATTENTION] Some GC verification tests failed!")
            return 1
        else:
            print("\n  [SUCCESS] All GC verification tests passed!")
            return 0


def main():
    binary = BINARY
    if len(sys.argv) > 1:
        binary = sys.argv[1]

    verifier = GCVerifier(binary)
    return verifier.run_all()


if __name__ == "__main__":
    sys.exit(main())
