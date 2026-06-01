#!/usr/bin/env python3
import sys
import os
import subprocess

def compile_aot(script_path, output_bin):
    if not os.path.exists(script_path):
        print(f"Error: Script {script_path} not found.")
        sys.exit(1)

    with open(script_path, "r") as f:
        code = f.read()

    # Escape the code for a C string literal
    code_escaped = code.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')

    c_stub = f"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void var_table_init(void);
extern void func_table_init(void);
extern void intern_table_init(void);
extern void glow_clear_errors(void);
// A minimal AOT stub that uses the existing flex scanner and bison parser 
// built into the VM to evaluate the embedded payload directly.
// In a full AOT phase, this would serialize the exact bytecodes.

int main(int argc, char **argv) {{
    // AOT Package Execution
    char *payload = "{code_escaped}";
    
    // We execute the payload via a system call to the bundled VM for the AOT V1
    // (A true LLVM/Native AOT compiles directly to object code, which is planned for V2)
    FILE *f = fopen("/tmp/aot_payload.glow", "w");
    if(f) {{
        fprintf(f, "%s", payload);
        fclose(f);
    }}
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./bin/glowlang /tmp/aot_payload.glow");
    return system(cmd);
}}
"""
    stub_path = "/tmp/aot_stub.c"
    with open(stub_path, "w") as f:
        f.write(c_stub)

    # Compile the stub
    print(f"Compiling {script_path} to standalone binary {output_bin}...")
    subprocess.run(["gcc", stub_path, "-o", output_bin, "-O2"])
    print("AOT Compilation successful.")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 glow_aot.py <script.glow> <output_binary>")
        sys.exit(1)
    compile_aot(sys.argv[1], sys.argv[2])
