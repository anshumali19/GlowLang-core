import re

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

ast_c_content = """#include "glowlang.h"
#include <stdlib.h>
#include <string.h>

extern const char *intern_string(const char *s);
extern void yyerror(const char *s);
extern Value value_string(const char *s);

""" + '\n'.join(ast_lines)

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

new_y_lines = y_lines[:start_idx] + y_lines[end_idx:]
y_content = '\n'.join(new_y_lines)

# Move ONLY the basic value stuff to glowlang.h
# But wait, instead of moving them, why not just export them or define them in ast.c?
# It's cleaner to put them in glowlang.h
# But we can also just declare make_value and value_int etc in ast.c? NO, they are inline in glowlang.y.
# Let's just move MASK_NAN, MASK_TYPE, MASK_PAYLOAD, make_value, value_int, value_float, value_bool, GET_TYPE
# and leave the rest in glowlang.y!

value_block = """
#define MASK_NAN      ((uint64_t)0x7FF8000000000000)
#define MASK_TYPE     ((uint64_t)0x0007000000000000)
#define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)

static inline Value make_value(uint64_t type, uint64_t payload) {
    Value v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);
    return v;
}

static inline Value value_int(int64_t v) { return make_value(VAL_INT, (uint64_t)v); }
static inline Value value_float(double v) { Value val; memcpy(&val, &v, sizeof(v)); return val; }
static inline Value value_bool(int v) { return make_value(VAL_BOOL, v ? 1 : 0); }
"""

# Remove them from y_content
y_content = y_content.replace('#define MASK_NAN      ((uint64_t)0x7FF8000000000000)\n#define MASK_TYPE     ((uint64_t)0x0007000000000000)\n#define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)\n', '')
y_content = y_content.replace('static inline Value make_value(uint64_t type, uint64_t payload) {\n    Value v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);\n    return v;\n}\n', '')
y_content = y_content.replace('static inline Value value_int(int64_t v) { return make_value(VAL_INT, (uint64_t)v); }\n', '')
y_content = y_content.replace('static inline Value value_float(double v) { Value val; memcpy(&val, &v, sizeof(v)); return val; }\n', '')
y_content = y_content.replace('static inline Value value_bool(int v) { return make_value(VAL_BOOL, v ? 1 : 0); }\n', '')

# Make value_string non-static
y_content = y_content.replace('static Value value_string(const char *s)', 'Value value_string(const char *s)')

with open('src/glowlang.y', 'w') as f:
    f.write(y_content)

# Update glowlang.h
with open('src/glowlang.h', 'r') as f:
    h_content = f.read()

if '#include <string.h>' not in h_content:
    h_content = h_content.replace('#include <stdint.h>', '#include <stdint.h>\n#include <string.h>\n#include <stdio.h>')

# Insert value_block right after typedef uint64_t Value;
insert_idx = h_content.find('typedef uint64_t Value;') + len('typedef uint64_t Value;')
h_content = h_content[:insert_idx] + '\n\n' + value_block + h_content[insert_idx:]

proto_str = '\n'.join(prototypes)
h_content = h_content.replace('#endif', f'{proto_str}\n\n#endif')

with open('src/glowlang.h', 'w') as f:
    f.write(h_content)

