import socket
import json
import time
import subprocess
import sys

def send_msg(sock, msg):
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    sock.sendall(header.encode('ascii') + content.encode('ascii'))

def read_msg(sock):
    # Read header
    header = b""
    while True:
        c = sock.recv(1)
        if not c: return None
        header += c
        if header.endswith(b"\r\n\r\n"): break
    
    # Parse length
    header_str = header.decode('ascii')
    length = 0
    for line in header_str.split("\r\n"):
        if line.startswith("Content-Length: "):
            length = int(line[16:])
    
    # Read body
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk: break
        body += chunk
    return json.loads(body.decode('ascii'))

def verify():
    port = 9999
    print("Building...")
    subprocess.run(["flex", "glowlang.l"], check=True)
    subprocess.run(["bison", "-d", "glowlang.y"], check=True)
    subprocess.run(["gcc", "lex.yy.c", "glowlang.tab.c", "lsp.c", "-o", "glowlang", "-lfl", "-O2", "-lm"], check=True)

    print(f"Starting LSP server on port {port}...")
    server = subprocess.Popen(['./glowlang', '--lsp-port', str(port)])
    time.sleep(1) # Wait for server to bind

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('127.0.0.1', port))
        
        # 1. Initialize
        print("Test 1: Initialize... ", end="", flush=True)
        send_msg(sock, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        resp = read_msg(sock)
        if resp and "result" in resp:
            print("PASS")
        else:
            print(f"FAIL (Response: {resp})")
            return

        # 2. Diagnostics
        print("Test 2: Diagnostics... ", end="", flush=True)
        send_msg(sock, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file:///test.glow",
                    "text": "when true\n  show 1\n"
                }
            }
        })
        diag = read_msg(sock)
        if diag and "method" in diag and diag["method"] == "textDocument/publishDiagnostics":
            errors = diag["params"]["diagnostics"]
            if len(errors) > 0:
                print(f"PASS (Found {len(errors)} errors as expected)")
            else:
                print("FAIL (No errors found in invalid code)")
        else:
            print(f"FAIL (Response: {diag})")

    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        server.terminate()

if __name__ == "__main__":
    verify()
