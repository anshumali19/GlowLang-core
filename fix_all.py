import re

# 1. Read glowlang.y
with open('src/glowlang.y', 'r') as f:
    y_content = f.read()

# Find the block starting with #define MASK_NAN and ending with value_none
start_str = "#define MASK_NAN"
end_str = "static inline Value value_none(void) { return make_value(VAL_NONE, 0); }"

start_idx = y_content.find(start_str)
end_idx = y_content.find(end_str) + len(end_str)

if start_idx != -1 and end_idx != -1:
    value_block = y_content[start_idx:end_idx]
    
    # Remove it from glowlang.y
    y_content = y_content[:start_idx] + y_content[end_idx:]

    # Remove static inline and make them just inline or remove static
    # Or just keep static inline in the header! It's perfectly fine for header.

# Find value_string in glowlang.y and remove 'static '
y_content = y_content.replace('static Value value_string(const char *s) {', 'Value value_string(const char *s) {')

with open('src/glowlang.y', 'w') as f:
    f.write(y_content)

# 2. Update glowlang.h
with open('src/glowlang.h', 'r') as f:
    h_content = f.read()

# Add stdio.h
if '#include <stdio.h>' not in h_content:
    h_content = h_content.replace('#include <stdint.h>', '#include <stdint.h>\n#include <stdio.h>')

# Insert value_block after #define VAL_NONE
val_none_idx = h_content.find('#define VAL_NONE VAL_OBJECT\n\ntypedef uint64_t Value;')
insert_idx = val_none_idx + len('#define VAL_NONE VAL_OBJECT\n\ntypedef uint64_t Value;')

if start_idx != -1:
    h_content = h_content[:insert_idx] + '\n\n' + value_block + '\n' + h_content[insert_idx:]

# Read prototypes from split_ast.py output
with open('split_ast.py', 'r') as f:
    pass # Wait, we already have them printed or we can generate again

# Generate prototypes again
with open('src/ast.c', 'r') as f:
    ast_c_content = f.read()

prototypes = []
for line in ast_c_content.split('\n'):
    if line.startswith('Node *'):
        # It's a definition like Node *newIntNode(int v) {
        proto = line.replace(' {', ';')
        prototypes.append(proto)
    elif line.startswith('int ast_contains_let(Node *n) {'):
        prototypes.append('int ast_contains_let(Node *n);')

prototypes_str = '\n'.join(prototypes)

# Insert prototypes at the end of glowlang.h before #endif
h_content = h_content.replace('#endif', f'{prototypes_str}\n\n#endif')

with open('src/glowlang.h', 'w') as f:
    f.write(h_content)

print("Done fixing.")
