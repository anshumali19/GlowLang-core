with open("src/vm.c", "r") as f:
    content = f.read()

import re

# In OP_CALL_VALUE for OBJ_BOUND_METHOD:
# replace "int callee_base = base_reg + pc->r2;" with "int callee_base = base_reg + bc->local_count;"
# wait, there are multiple places.

content = content.replace(
    "int callee_base = base_reg + pc->r2;",
    "int callee_base = base_reg + bc->local_count;"
)

# Wait, in OP_CALL_VALUE for OBJ_CLOSURE:
# DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);
# change to DO_CALL(c->body, c, base_reg + bc->local_count, pc->r0);
content = content.replace(
    "DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);",
    "int callee_base = base_reg + bc->local_count;\n                    for (int i=0; i<ac; i++) vm_stack[callee_base + i] = R(pc->r2 + i);\n                    DO_CALL(c->body, c, callee_base, pc->r0);"
)

with open("src/vm.c", "w") as f:
    f.write(content)
