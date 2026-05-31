
import subprocess
import os

os.chdir("benchmarks/cross")

def run(cmd):
    print(f"Running: {cmd}")
    subprocess.run(cmd, shell=True, check=True)

# C
run("gcc -O3 fib.c -o fib_c")
run("gcc -O3 loop.c -o loop_c")
run("gcc -O3 ack.c -o ack_c")
run("gcc -O3 nq.c -o nq_c")
run("gcc -O3 dijkstra.c -o dijkstra_c")

# C++
run("g++ -O3 fib.cpp -o fib_cpp")
run("g++ -O3 loop.cpp -o loop_cpp")
run("g++ -O3 ack.cpp -o ack_cpp")
run("g++ -O3 nq.cpp -o nq_cpp")
run("g++ -O3 dijkstra.cpp -o dijkstra_cpp")

# Go
run("go build -o fib_go fib.go")
run("go build -o loop_go loop.go")
run("go build -o ack_go ack.go")
run("go build -o nq_go nq.go")
run("go build -o dijkstra_go dijkstra.go")

# Java
run("javac Fib.java Loop.java Ack.java NQ.java Dijkstra.java")

# C# (Using dotnet)
# We'll create one project and swap Program.cs for each build
run("dotnet new console -n CSharpProject --force")
os.chdir("CSharpProject")
for bench in ["Fib", "Loop", "Ack", "NQ", "Dijkstra"]:
    run(f"cp ../{bench}.cs Program.cs")
    run(f"dotnet build -c Release")
    run(f"cp bin/Release/net8.0/CSharpProject ../{bench.lower()}_cs")
os.chdir("..")

print("Compilation complete.")
