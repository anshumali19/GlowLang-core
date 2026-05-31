import re

assign_pattern = re.compile(r'^(\s*)([a-zA-Z_]\w*)\s*=\s*(?![=])(.*)')

test_lines = [
    "    mid = (left + right) / 2",
    "    n1 = mid - left + 1",
    "    L = []",
    "    i = 0",
    "    arr[k] = L[i]",
    "    self.count = 0"
]

for line in test_lines:
    am = assign_pattern.match(line)
    if am:
        print(f"Matched: {am.groups()}")
    else:
        print(f"Failed: {line}")
