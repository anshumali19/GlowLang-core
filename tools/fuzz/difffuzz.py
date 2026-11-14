#!/usr/bin/env python3
"""Differential fuzzer for GlowLang.

Each round writes one random program twice, once in GlowLang and once in
Python, with the same meaning. It then runs it three ways:

    python3                         the reference answer
    glowlang  (GLOW_NOJITEXEC=1)    the bytecode VM only
    glowlang                        the VM with the JIT

All three outputs must match. A crash, a hang, or any difference is saved in
the output folder as NNNN.glow plus NNNN.py, together with a NNNN.txt that
says what went wrong.

The programs use only features whose meaning is the same in both languages:
integers kept small by m(), lists, if/eif/else, while, every, for each,
functions (some recursive), classes, and try/throw.

    python3 tools/fuzz/difffuzz.py --glow bin/glowlang --rounds 500
    python3 tools/fuzz/difffuzz.py --seed 1234 --rounds 1 --keep   # replay one
"""

import argparse
import os
import random
import re
import subprocess
import sys
import time

MOD = 1000003


class Gen:
    def __init__(self, rng):
        self.r = rng
        self.g = []   # GlowLang lines
        self.p = []   # Python lines
        self.ind = 0

    def emit(self, glow, py=None):
        pad = "    " * self.ind
        self.g.append(pad + glow)
        self.p.append(pad + (glow if py is None else py))

    # -- expressions: returns (glow, python) --------------------------------
    def expr(self, names, funcs, depth=0):
        r = self.r
        choice = r.random()
        if depth > 2 or choice < 0.25:
            if names and r.random() < 0.7:
                v = r.choice(names)
                return v, v
            n = str(r.randint(0, 50))
            return n, n
        if choice < 0.75:
            op = r.choice(["+", "-", "*"])
            a, pa = self.expr(names, funcs, depth + 1)
            b, pb = self.expr(names, funcs, depth + 1)
            return f"m({a} {op} {b})", f"m({pa} {op} {pb})"
        if choice < 0.85 and funcs:
            f, arity = r.choice(funcs)
            args = [self.expr(names, funcs, depth + 1) for _ in range(arity)]
            return (f"{f}(" + ", ".join(a for a, _ in args) + ")",
                    f"{f}(" + ", ".join(p for _, p in args) + ")")
        if choice < 0.93:
            a, pa = self.expr(names, funcs, depth + 1)
            return f"m(len(xs) + {a})", f"m(len(xs) + {pa})"
        a, pa = self.expr(names, funcs, depth + 1)
        return f"xs[m({a}) % len(xs)]", f"xs[m({pa}) % len(xs)]"

    def cond(self, names, funcs):
        a, pa = self.expr(names, funcs, 1)
        b, pb = self.expr(names, funcs, 1)
        op = self.r.choice(["<", "<=", ">", ">=", "==", "!="])
        g, p = f"{a} {op} {b}", f"{pa} {op} {pb}"
        k = self.r.random()
        if k < 0.15:
            return f"not ({g})", f"not ({p})"
        if k < 0.3:
            c, pc = self.expr(names, funcs, 2)
            return f"({g}) and {c} > 10", f"({p}) and {pc} > 10"
        if k < 0.4:
            c, pc = self.expr(names, funcs, 2)
            return f"({g}) or {c} < 5", f"({p}) or {pc} < 5"
        return g, p

    @staticmethod
    def writable(names):
        # Loop variables are read-only: assigning to one changes how many
        # times an `every` loop runs, while Python's for ignores it.
        return [n for n in names if not (n == "round" or re.match(r"[it]\d+$", n))]

    # -- statements -------------------------------------------------------
    def block(self, names, funcs, n, depth):
        for _ in range(n):
            self.stmt(names, funcs, depth)

    def stmt(self, names, funcs, depth):
        r = self.r
        k = r.random()
        can_write = self.writable(names)
        if k < 0.35 or depth > 2 or not can_write:
            if can_write and r.random() < 0.5:
                v = r.choice(can_write)
                e, pe = self.expr(names, funcs)
                self.emit(f"{v} = m({e})", f"{v} = m({pe})")
            else:
                v = f"v{len(names)}_{r.randint(0, 999)}"
                e, pe = self.expr(names, funcs)
                self.emit(f"let {v} = m({e})", f"{v} = m({pe})")
                names.append(v)
        elif k < 0.5:
            c, pc = self.cond(names, funcs)
            self.emit(f"if {c}:", f"if {pc}:")
            self.ind += 1
            self.block(list(names), funcs, r.randint(1, 2), depth + 1)
            self.ind -= 1
            if r.random() < 0.5:
                c2, pc2 = self.cond(names, funcs)
                self.emit(f"eif {c2}:", f"elif {pc2}:")
                self.ind += 1
                self.block(list(names), funcs, 1, depth + 1)
                self.ind -= 1
            if r.random() < 0.5:
                self.emit("else:")
                self.ind += 1
                self.block(list(names), funcs, 1, depth + 1)
                self.ind -= 1
            self.emit("end", "pass")
        elif k < 0.62:
            hi = r.randint(0, 12)
            iv = f"i{depth}"
            self.emit(f"every {iv} from 0 to {hi}:", f"for {iv} in range(0, {hi + 1}):")
            self.ind += 1
            self.block(names + [iv], funcs, r.randint(1, 2), depth + 1)
            self.ind -= 1
            self.emit("end", "pass")
        elif k < 0.72:
            w = f"w{depth}_{r.randint(0, 999)}"
            self.emit(f"let {w} = 0", f"{w} = 0")
            self.emit(f"while {w} < {r.randint(1, 15)}:")
            self.ind += 1
            self.emit(f"{w} = {w} + 1")
            if r.random() < 0.3:
                self.emit(f"if {w} == 3:")
                self.ind += 1
                self.emit(r.choice(["skip", "stop"]) if True else "", None)
                # Python spelling of the same thing
                self.p[-1] = self.p[-1].replace("skip", "continue").replace("stop", "break")
                self.ind -= 1
                self.emit("end", "pass")
            self.block(names + [w], funcs, 1, depth + 1)
            self.ind -= 1
            self.emit("end", "pass")
            names.append(w)
        elif k < 0.8:
            e, pe = self.expr(names, funcs)
            self.emit(f"xs.append(m({e}))", f"xs.append(m({pe}))")
        elif k < 0.87:
            t = f"t{depth}"
            acc = r.choice(can_write)
            self.emit(f"for each {t} in xs[0:4]:", f"for {t} in xs[0:4]:")
            self.ind += 1
            self.emit(f"{acc} = m({acc} + {t})")
            self.ind -= 1
            self.emit("end", "pass")
        elif k < 0.94:
            e, pe = self.expr(names, funcs)
            v = r.choice(can_write)
            self.emit("try:")
            self.ind += 1
            self.emit(f"if m({e}) % 3 == 0:", f"if m({pe}) % 3 == 0:")
            self.ind += 1
            self.emit('throw "odd"', 'raise Boom("odd")')
            self.ind -= 1
            self.emit("end", "pass")
            self.emit(f"{v} = m({v} + 7)")
            self.ind -= 1
            self.emit("catch e:", "except Boom as e:")
            self.ind += 1
            self.emit(f"{v} = m({v} + 11)")
            self.ind -= 1
            self.emit("end", "pass")
        else:
            v = r.choice(can_write)
            self.emit(f"box.add({v})")
            self.emit(f"{v} = m({v} + box.total)")

    # -- whole program ------------------------------------------------------
    def program(self):
        r = self.r
        self.g.append(f"func m(x):\n    if x < 0:\n        x = 0 - x\n    end\n    return x % {MOD}\nend")
        self.p.append(f"class Boom(Exception):\n    pass\ndef m(x):\n    if x < 0:\n        x = 0 - x\n    return x % {MOD}")
        self.g.append("class Box:\n    func init():\n        self.total = 0\n    end\n"
                      "    func add(n):\n        self.total = m(self.total + n)\n        return self\n    end\nend")
        self.p.append("class Box:\n    def __init__(self):\n        self.total = 0\n"
                      "    def add(self, n):\n        self.total = m(self.total + n)\n        return self")
        self.g.append("let xs = [1, 2, 3]\nlet box = Box()")
        self.p.append("xs = [1, 2, 3]\nbox = Box()")

        funcs = []
        for fi in range(r.randint(1, 4)):
            name = f"f{fi}"
            arity = r.randint(1, 3)
            params = [f"p{j}" for j in range(arity)]
            self.emit(f"func {name}({', '.join(params)}):",
                      f"def {name}({', '.join(params)}):")
            self.ind += 1
            if fi == 0 and r.random() < 0.5:
                # bounded recursion, also exercises tail calls
                self.emit("if p0 % 16 < 1:", "if p0 % 16 < 1:")
                self.ind += 1
                self.emit("return 1")
                self.ind -= 1
                self.emit("end", "pass")
                rest = ", ".join(params[1:])
                call = f"{name}(m(p0 - 1){', ' + rest if rest else ''})"
                if r.random() < 0.5:
                    self.emit(f"return {call}")
                else:
                    self.emit(f"return m(p0 + {call})")
            else:
                local = list(params)
                self.block(local, funcs, r.randint(1, 4), 1)
                e, pe = self.expr(local, funcs)
                self.emit(f"return m({e})", f"return m({pe})")
            self.ind -= 1
            self.emit("end", "")
            funcs.append((name, arity))

        names = []
        loops = r.choice([3, 60, 1200])  # 1200 passes the loop-JIT threshold
        self.emit("let acc = 0", "acc = 0")
        self.emit("let xs_cap = 0", "xs_cap = 0")
        self.emit(f"every round from 1 to {loops}:", f"for round in range(1, {loops + 1}):")
        self.ind += 1
        self.emit("if len(xs) > 40:")
        self.ind += 1
        self.emit("xs = xs[0:3]")
        self.ind -= 1
        self.emit("end", "pass")
        inner = ["round", "acc"]
        self.block(inner, funcs, r.randint(2, 6), 1)
        e, pe = self.expr(inner, funcs)
        self.emit(f"acc = m(acc + {e})", f"acc = m(acc + {pe})")
        self.ind -= 1
        self.emit("end", "pass")
        self.emit("show acc", "print(acc)")
        self.emit("show xs", "print('[' + ', '.join(str(x) for x in xs) + ']')")
        self.emit("show box.total", "print(box.total)")
        return "\n".join(self.g) + "\n", "\n".join(self.p) + "\n"


def run(cmd, env=None, timeout=20):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           env=env, errors="replace")
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glow", default="bin/glowlang")
    ap.add_argument("--rounds", type=int, default=200)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--out", default="fuzz-out")
    ap.add_argument("--keep", action="store_true", help="save every program, not only failures")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    base = a.seed if a.seed is not None else int(time.time())
    novm = dict(os.environ, GLOW_NOJITEXEC="1")
    bad = skipped = 0

    for i in range(a.rounds):
        seed = base + i
        g, p = Gen(random.Random(seed)).program()
        gpath = os.path.join(a.out, f"{seed}.glow")
        ppath = os.path.join(a.out, f"{seed}.py")
        with open(gpath, "w") as f:
            f.write(g)
        with open(ppath, "w") as f:
            f.write(p)

        rc_py, want, perr = run([sys.executable, "-I", ppath])
        if rc_py != 0:
            # A generator bug, not a GlowLang bug: report it but keep going.
            skipped += 1
            print(f"seed {seed}: python failed ({rc_py}): {perr.strip().splitlines()[-1:]}")
            continue
        rc_vm, out_vm, err_vm = run([a.glow, gpath], env=novm)
        rc_jit, out_jit, err_jit = run([a.glow, gpath])

        problems = []
        for label, rc, out, err in (("vm", rc_vm, out_vm, err_vm), ("jit", rc_jit, out_jit, err_jit)):
            if rc == "timeout":
                problems.append(f"{label}: timed out")
            elif rc != 0:
                problems.append(f"{label}: exit {rc}: {err.strip()[-400:]}")
            elif out != want:
                problems.append(f"{label}: wrong output\n  want {want!r}\n  got  {out!r}")
        if problems:
            bad += 1
            with open(os.path.join(a.out, f"{seed}.txt"), "w") as f:
                f.write("\n".join(problems) + "\n")
            print(f"seed {seed}: FAIL\n  " + "\n  ".join(problems))
        elif not a.keep:
            os.remove(gpath)
            os.remove(ppath)

    print(f"\n{a.rounds} programs, {bad} failing, {skipped} skipped (generator)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
