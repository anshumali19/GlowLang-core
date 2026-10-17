#!/usr/bin/env python3
"""
GlowLang Standalone Binary Packager
Wraps the native in-process binary packaging engine (`glowlang package`).
Produces self-contained standalone executables embedding runtime and bytecode.
"""
import sys
import os
import subprocess

def find_glowlang_bin():
    # Search common binary locations
    base = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    candidates = [
        os.path.join(base, "bin", "glowlang.exe"),
        os.path.join(base, "bin", "glowlang"),
        os.path.join(base, "glowlang.exe"),
        os.path.join(base, "glowlang"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return "glowlang"

def package_script(script_path, output_bin):
    if not os.path.exists(script_path):
        print(f"Error: Script '{script_path}' not found.")
        sys.exit(1)

    glowlang = find_glowlang_bin()
    cmd = [glowlang, "package", os.path.abspath(script_path), "-o", os.path.abspath(output_bin)]
    print(f"Packaging {script_path} into self-contained binary {output_bin}...")
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print(f"Standalone package successfully created: {output_bin}")
    else:
        print(f"Packaging failed with exit code {res.returncode}")
        sys.exit(res.returncode)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 glow_aot.py <script.glow> <output_binary>")
        sys.exit(1)
    package_script(sys.argv[1], sys.argv[2])
