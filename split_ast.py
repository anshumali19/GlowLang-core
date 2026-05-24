import os

with open('src/glowlang.y', 'r') as f:
    y_lines = f.read().split('\n')

# Find AST CONSTRUCTORS block
start_idx = -1
end_idx = -1
for i, line in enumerate(y_lines):
    if "DYNAMIC AST CONSTRUCTORS" in line:
        start_idx = i - 1
    if start_idx != -1 and i > start_idx + 10 and "GLOBALS" in line:
        end_idx = i - 1
        break

ast_lines = y_lines[start_idx:end_idx]

# Create ast.c
ast_c_content = """#include "glowlang.h"
#include <stdlib.h>
#include <string.h>

extern const char *intern_string(const char *s);
extern void yyerror(const char *s);
Value value_string(const char *s);

""" + '\n'.join(ast_lines)

# Clean up static from node constructors in ast_c_content
# and collect prototypes
import re
prototypes = []
modified_ast_lines = []
for line in ast_c_content.split('\n'):
    if line.startswith('static Node *'):
        line = line.replace('static ', '')
        m = re.match(r'Node \*([a-zA-Z0-9_]+)\(', line)
        if m:
            prototypes.append(line.replace(' {', ';'))
    elif line.startswith('static int ast_contains_let'):
        line = line.replace('static ', '')
        prototypes.append('int ast_contains_let(Node *n);')
    modified_ast_lines.append(line)

ast_c_content = '\n'.join(modified_ast_lines)

with open('src/ast.c', 'w') as f:
    f.write(ast_c_content)

# Update glowlang.y (remove AST constructors)
new_y_lines = y_lines[:start_idx] + y_lines[end_idx:]
with open('src/glowlang.y', 'w') as f:
    f.write('\n'.join(new_y_lines))

# Generate prototypes string
proto_str = "\n".join(prototypes)

print("Prototypes found:")
print(proto_str)
