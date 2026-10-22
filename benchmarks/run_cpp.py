import subprocess, time
start = time.time()
result = subprocess.run(['ack_cpp'], capture_output=True)
end = time.time()
print(f'C++: {end - start:.3f}s')
print(result.stdout.decode())