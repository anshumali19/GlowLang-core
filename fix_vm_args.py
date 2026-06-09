import re

with open("src/vm.c", "r") as f:
    content = f.read()

# OP_CALL_METHOD block
# find:
#             vm_stack[callee_base] = obj; // set 'self'
#             DO_CALL(fe->body, NULL, callee_base, pc->r0);
# replace:
#             vm_stack[callee_base] = obj; // set 'self'
#             for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);
#             DO_CALL(fe->body, NULL, callee_base, pc->r0);

content = content.replace(
    "vm_stack[callee_base] = obj; // set 'self'\n            DO_CALL(fe->body, NULL, callee_base, pc->r0);",
    "vm_stack[callee_base] = obj; // set 'self'\n            for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);\n            DO_CALL(fe->body, NULL, callee_base, pc->r0);"
)

# OP_CALL_VALUE -> BOUND_METHOD
# find:
#                     if (GET_TYPE(bm->instance) != VAL_NONE) {
#                         vm_stack[callee_base] = bm->instance; // self at R0
#                     }
#                     DO_CALL(bm->method->body, NULL, callee_base, pc->r0);
# replace:
#                     if (GET_TYPE(bm->instance) != VAL_NONE) {
#                         vm_stack[callee_base] = bm->instance; // self at R0
#                     }
#                     for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);
#                     DO_CALL(bm->method->body, NULL, callee_base, pc->r0);

content = content.replace(
    "if (GET_TYPE(bm->instance) != VAL_NONE) {\n                        vm_stack[callee_base] = bm->instance; // self at R0\n                    }\n                    DO_CALL(bm->method->body, NULL, callee_base, pc->r0);",
    "if (GET_TYPE(bm->instance) != VAL_NONE) {\n                        vm_stack[callee_base] = bm->instance; // self at R0\n                    }\n                    for (int i=0; i<ac; i++) vm_stack[callee_base + 1 + i] = R(pc->r2 + i);\n                    DO_CALL(bm->method->body, NULL, callee_base, pc->r0);"
)

with open("src/vm.c", "w") as f:
    f.write(content)
