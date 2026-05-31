import re

with open("src/glowlang.y", "r") as f:
    content = f.read()

match = re.search(r"static void\*\s*dt\[\]\s*=\s*\{(.*?)\};", content, re.DOTALL)
if not match:
    print("Could not find jump table")
    exit(1)

labels = re.findall(r"&&l_(OP_\w+)", match.group(1))

print("typedef enum {")
for i, label in enumerate(labels):
    comma = "," if i < len(labels) - 1 else ""
    print(f"    {label}{comma}")
print("} OpCode;")
