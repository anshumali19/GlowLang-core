import re

with open("lib/tensor_utils.glow", "r") as f:
    content = f.read()

# Replace 'for var from start to end:' with 'for var from start to end - 1:'
pattern = r"for (\w+) from (\d+) to (\w+):"
replaced = re.sub(pattern, r"for \1 from \2 to \3 - 1:", content)

with open("lib/tensor_utils.glow", "w") as f:
    f.write(replaced)

print("Loop bounds successfully adjusted to exclude upper bound.")
