gdb -q -ex "set pagination off" -ex "run" -ex "x/20i \$pc-20" -ex "info registers" -ex "bt" -ex "quit" --args bin/glowlang benchmarks/bench_jit_single.glow > gdb_out.txt 2>&1
