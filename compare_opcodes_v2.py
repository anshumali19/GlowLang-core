import re

with open("src/glowlang.h", "r") as f:
    h_content = f.read()

with open("src/glowlang.y", "r") as f:
    y_content = f.read()

# Find OpCode enum
opcode_enum_match = re.search(r"typedef enum \{([^}]*)\} OpCode;", h_content, re.DOTALL)
if not opcode_enum_match:
    print("Could not find OpCode enum")
    exit(1)

opcodes = [line.split(',')[0].strip() for line in opcode_enum_match.group(1).split('\n') if "OP_" in line]

# Find jump table
dt_match = re.search(r"static void\* dt\[\] = \{(.*?)\};", y_content, re.DOTALL)
if not dt_match:
    print("Could not find jump table")
    exit(1)

dt_entries = re.findall(r"&&l_(OP_\w+)", dt_match.group(1))

print(f"Enum has {len(opcodes)} entries")
print(f"Jump table has {len(dt_entries)} entries")

for i in range(max(len(opcodes), len(dt_entries))):
    e = opcodes[i] if i < len(opcodes) else "MISSING"
    d = dt_entries[i] if i < len(dt_entries) else "MISSING"
    if e != d:
        print(f"Mismatch at index {i}: Enum={e}, JumpTable={d}")
    # else:
    #     print(f"Index {i}: {e} matches")
