import re

with open("src/vm.c", "r") as f:
    content = f.read()

# Fix OBJ_BOUND_METHOD in l_OP_CALL_VALUE
content = content.replace(
    "for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);",
    "int start_offset = pc->is_method ? 1 : 0;\n                    for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + start_offset + i);"
)

# Fix OBJ_CLOSURE in l_OP_CALL_VALUE
content = content.replace(
    "for (int i=0; i<ac; i++) vm_stack[callee_base + i] = R(pc->r2 + i);",
    "int start_offset = pc->is_method ? 1 : 0;\n                    for (int i=0; i<ac; i++) vm_stack[callee_base + i] = R(pc->r2 + start_offset + i);"
)

# Wait, what about l_OP_CALL?
# The same block exists for fallback_callee in l_OP_CALL_METHOD!
# Wait! In l_OP_CALL_METHOD, is_method is usually NOT set on OP_CALL_METHOD?
# Actually, OP_CALL_METHOD doesn't use `is_method` because it implicitly knows it's a method!
# But wait! OP_CALL_METHOD in opt.c keeps `call_pc->is_method` if it was set!
# But for OP_CALL_METHOD, `pc->r2` is `callee_frame_base`.
# In l_OP_CALL_METHOD:
# for (int i = 0; i < ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);
# If it's an OP_CALL_METHOD, compiler.c NEVER emits OP_CALL_METHOD!
# Only opt.c does! And opt.c creates it from OP_CALL_VALUE.
# So pc->is_method IS true!
# So we need start_offset there too!

content = content.replace(
    "for (int i = 0; i < ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);",
    "int start_offset = pc->is_method ? 1 : 0;\n            for (int i = 0; i < ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + start_offset + i);"
)

with open("src/vm.c", "w") as f:
    f.write(content)
