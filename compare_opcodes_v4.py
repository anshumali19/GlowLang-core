import re

def get_opcodes_h():
    with open("src/glowlang.h", "r") as f:
        content = f.read()
    match = re.search(r"typedef enum\s*\{(.*?)\}\s*OpCode\s*;", content, re.DOTALL)
    if not match: return []
    items = match.group(1).split(',')
    ops = []
    for item in items:
        item = item.strip()
        if item.startswith("OP_"):
            # Handle potential comments or assignments
            op = item.split()[0].split('=')[0].strip()
            ops.append(op)
    return ops

def get_opcodes_y():
    with open("src/glowlang.y", "r") as f:
        content = f.read()
    match = re.search(r"static void\*\s*dt\[\]\s*=\s*\{(.*?)\};", content, re.DOTALL)
    if not match: return []
    labels = re.findall(r"&&l_(OP_\w+)", match.group(1))
    return labels

ops_h = get_opcodes_h()
ops_y = get_opcodes_y()

print(f"H has {len(ops_h)} opcodes")
print(f"Y has {len(ops_y)} labels")

max_len = max(len(ops_h), len(ops_y))
for i in range(max_len):
    h = ops_h[i] if i < len(ops_h) else "MISSING"
    y = ops_y[i] if i < len(ops_y) else "MISSING"
    if h != y:
        print(f"{i:3}: H={h:20} Y={y:20} MISMATCH")
    # else:
    #    print(f"{i:3}: {h:20} MATCH")
