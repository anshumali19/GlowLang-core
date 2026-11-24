"""Types into `glowlang repl` through a pseudo-terminal and prints what the
REPL showed and what it saved in the history, one item per line, so
tests/test_repl_editing.glow can compare them.

    python3 tests/tools/repl_pty_check.py path/to/glowlang
"""
import os, pty, re, select, sys, tempfile, time

home = tempfile.mkdtemp()
pid, fd = pty.fork()
if pid == 0:
    os.environ["HOME"] = home
    os.environ["TERM"] = "xterm"
    os.execv(sys.argv[1], [sys.argv[1], "repl"])

out = b""
def send(keys, wait=0.3):
    global out
    os.write(fd, keys)
    end = time.time() + wait
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                return

UP, LEFT, HOME = b"\x1b[A", b"\x1b[D", b"\x1b[H"
send(b"", 1.0)
send(b"let x = 41\r")
send(b"show x + 1\r")                                    # 42
send(UP + b"\x7f2\r")                                    # recall, Backspace, 2: 43
send(b'show "caf\xc3\xa9"' + LEFT + LEFT + b"!\r")       # Left steps over the whole é
send(b"show 1 + 1" + HOME + b"\x05 + 1\r")               # Home, then Ctrl+E: 3
send(b"dropped\x03")                                     # Ctrl+C drops the line
send(b"junk\x15show 99\r")                               # Ctrl+U clears to the start
send(b'when x > 1:\r    show "in block"\rend\r')          # typed ahead: nothing lost
send(b"exit\r", 1.0)
os.waitpid(pid, 0)

text = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", out).replace(b"\r", b"").decode("utf-8", "replace")
for line in text.split("\n"):
    if line and not line.startswith(("glow>", "....", "GlowLang", "Type", "^C")):
        print("shown:", line)
for line in open(os.path.join(home, ".glowlang_history"), encoding="utf-8").read().splitlines():
    print("history:", line)
