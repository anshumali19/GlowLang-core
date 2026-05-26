import subprocess
with open("gdb_cmds.txt", "w") as f:
    f.write("set pagination off\nrun\nbt\nquit\n")
res = subprocess.run(["gdb", "-q", "-x", "gdb_cmds.txt", "--args", "bin/glowlang", "benchmarks/bench_jit.glow"], capture_output=True, text=True)
print(res.stdout)
