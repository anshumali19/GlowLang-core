import re

with open("src/glowlang.h", "r") as f:
    h_content = f.read()

with open("src/glowlang.y", "r") as f:
    y_content = f.read()

# Find OpCode enum precisely
# Match everything between "typedef enum {" and "} OpCode;"
match = re.search(r"typedef enum\s*\{(.*?)\}\s*OpCode\s*;", h_content, re.DOTALL)
if not match:
    print("Could not find OpCode enum")
    exit(1)

opcodes = []
for line in match.group(1).split('\n'):
    line = line.strip()
    if line.startswith("OP_"):
        # Remove trailing comma and whitespace
        op = line.split(',')[0].strip()
        opcodes.append(op)

# Find jump table precisely
match_y = re.search(r"static void\*\s*dt\[\]\s*=\s*\{(.*?)\};", y_content, re.DOTALL)
if not match_y:
    print("Could not find jump table in src/glowlang.y")
    exit(1)

dt_entries = re.findall(r"&&l_(OP_\w+)", match_y.group(1))

print(f"Enum has {len(opcodes)} opcodes")
print(f"Jump table has {len(dt_entries)} entries")

for i in range(max(len(opcodes), len(dt_entries))):
    e = opcodes[i] if i < len(opcodes) else "MISSING"
    d = dt_entries[i] if i < len(dt_entries) else "MISSING"
    if e != d:
        print(f"Mismatch at index {i}: Enum={e}, JumpTable={d}")
    else:
        print(f"Match at {i}: {e}")
