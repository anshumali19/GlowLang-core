import subprocess
import json
import time
import sys
import os

def send_msg(proc, msg):
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    proc.stdin.write(header.encode('ascii'))
    proc.stdin.write(content.encode('ascii'))
    proc.stdin.flush()
    print(f"SENT: {content}")

def read_msg(proc):
    # Non-blocking read would be better, but let's try to be clever with readline
    line = proc.stdout.readline().decode('ascii')
    if not line:
        return None
    if not line.startswith("Content-Length: "):
        print(f"DEBUG: Unexpected line from stdout: {line.strip()}")
        return None
    length = int(line[16:])
    # Read until the double newline
    while True:
        line = proc.stdout.readline().decode('ascii')
        if line == "\r\n" or line == "\n" or not line:
            break
    body = proc.stdout.read(length).decode('ascii')
    return json.loads(body)

# Build the latest version
print("Building GlowLang...")
try:
    subprocess.run(["flex", "glowlang.l"], check=True, capture_output=True)
    subprocess.run(["bison", "-d", "glowlang.y"], check=True, capture_output=True)
    subprocess.run(["gcc", "lex.yy.c", "glowlang.tab.c", "lsp.c", "-o", "glowlang", "-lfl", "-O2", "-lm"], check=True, capture_output=True)
except subprocess.CalledProcessError as e:
    print(f"Build failed: {e.stderr.decode('ascii')}")
    sys.exit(1)

# Start process
proc = subprocess.Popen(['./glowlang', '--lsp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)

def test():
    try:
        print("\n--- Testing 'initialize' ---")
        send_msg(proc, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {}
        })
        resp = read_msg(proc)
        print(f"RECV: {resp}")

        print("\n--- Testing 'textDocument/didOpen' with invalid code ---")
        send_msg(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file:///test.glow",
                    "text": "when true\n  show 1\n" # Missing colon
                }
            }
        })
        
        diag = read_msg(proc)
        print(f"RECV (Diagnostics): {diag}")

    except Exception as e:
        print(f"Test encountered an error: {e}")
    finally:
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=2)
        if stderr:
            print("\n--- SERVER STDERR ---")
            print(stderr.decode('ascii'))

if __name__ == "__main__":
    test()
