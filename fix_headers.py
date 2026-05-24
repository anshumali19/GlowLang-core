import re

with open("src/glowlang.h", "r") as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if line.startswith("#define MASK_NAN") and not skip:
        # We hit the duplicate block that causes errors
        skip = True
    if skip and line.startswith("/* Forward declarations */"):
        skip = False
    
    if not skip:
        new_lines.append(line)

# Let's completely rewrite the top part of glowlang.h
# It's better to just write a clean glowlang.h
