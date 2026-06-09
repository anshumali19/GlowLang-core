import subprocess
import json
import time
import sys

def send_msg(proc, msg):
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    proc.stdin.write(header.encode('ascii'))
    proc.stdin.flush()
    proc.stdin.write(content.encode('ascii'))
    proc.stdin.flush()

def read_msg(proc):
    line = proc.stdout.readline().decode('ascii')
    if not line: return None
    if not line.startswith("Content-Length: "): return None
    length = int(line[16:])
    while True:
        line = proc.stdout.readline().decode('ascii')
        if line in ["\r\n", "\n", ""]: break
    body = proc.stdout.read(length).decode('ascii')
    return json.loads(body)

def verify():
    print("Building...")
    subprocess.run(["flex", "glowlang.l"], check=True)
    subprocess.run(["bison", "-d", "glowlang.y"], check=True)
    subprocess.run(["gcc", "lex.yy.c", "glowlang.tab.c", "lsp.c", "-o", "glowlang", "-lfl", "-O2", "-lm"], check=True)

    proc = subprocess.Popen(['./glowlang', '--lsp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    try:
        # 1. Initialize
        print("Test 1: Initialize... ", end="", flush=True)
        send_msg(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        resp = read_msg(proc)
        if resp and "result" in resp:
            print("PASS")
        else:
            print(f"FAIL (Response: {resp})")
            return

        # 2. Diagnostics
        print("Test 2: Diagnostics... ", end="", flush=True)
        send_msg(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file:///test.glow",
                    "text": "when true\n  show 1\n"
                }
            }
        })
        diag = read_msg(proc)
        if diag and "method" in diag and diag["method"] == "textDocument/publishDiagnostics":
            errors = diag["params"]["diagnostics"]
            if len(errors) > 0:
                print(f"PASS (Found {len(errors)} errors as expected)")
            else:
                print("FAIL (No errors found in invalid code)")
        else:
            print(f"FAIL (Response: {diag})")

    finally:
        proc.terminate()

if __name__ == "__main__":
    verify()
