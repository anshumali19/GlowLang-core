import re

with open("src/glowlang.h", "r") as f:
    h_content = f.read()

with open("src/glowlang.y", "r") as f:
    y_content = f.read()

enum_match = re.search(r"typedef enum\s+\{(.*?)\}\s+OpCode;", h_content, re.DOTALL)
if not enum_match:
    print("Could not find OpCode enum in src/glowlang.h")
    exit(1)

enum_str = enum_match.group(1)
opcodes = [line.split(',')[0].strip() for line in enum_str.split('\n') if "OP_" in line]

dt_match = re.search(r"static void\* dt\[\] = \{(.*?)\};", y_content, re.DOTALL)
if not dt_match:
    print("Could not find dt array in src/glowlang.y")
    exit(1)

dt_str = dt_match.group(1)
dt_labels = [line.strip().replace("&&l_", "").replace(",", "") for line in dt_str.split('\n') if "&&l_OP_" in line]

# Flatten dt_labels as some might be on the same line
dt_labels_flat = []
for line in dt_str.split('\n'):
    matches = re.findall(r"&&l_(OP_\w+)", line)
    dt_labels_flat.extend(matches)

print(f"Enum has {len(opcodes)} opcodes")
print(f"Jump table has {len(dt_labels_flat)} entries")

for i in range(max(len(opcodes), len(dt_labels_flat))):
    e = opcodes[i] if i < len(opcodes) else "MISSING"
    d = dt_labels_flat[i] if i < len(dt_labels_flat) else "MISSING"
    if e != d:
        print(f"Mismatch at index {i}: Enum={e}, JumpTable={d}")
    # else:
    #    print(f"Index {i}: {e}")
