import subprocess, time
start = time.time()
r = subprocess.run(['bench_fib.exe'], capture_output=True)
end = time.time()
print(f'C++: {end - start:.3f}s')
print(r.stdout.decode())