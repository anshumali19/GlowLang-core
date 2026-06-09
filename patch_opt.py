with open("src/compiler.c", "r") as f:
    content = f.read()

import re

old_compile_expr = """int compile_expr(Node *n, int target_reg) {
    int reg = target_reg;
    if (reg == -1) reg = next_free_reg++;"""

new_compile_expr = """int compile_expr(Node *n, int target_reg) {
    if (!n) return target_reg == -1 ? 0 : target_reg;
    if (target_reg == -1 && n->type == NODE_VAR) {
        int src = get_local_reg(n->name);
        if (src != -1) return src;
    }
    int reg = target_reg;
    if (reg == -1) reg = next_free_reg++;"""

content = content.replace(old_compile_expr, new_compile_expr)

old_node_var = """        case NODE_VAR: {
            int src = get_local_reg(n->name);
            if (src != -1) {
                if (reg != src) bc_emit_reg(compiling_bc, OP_MOVE, reg, src, 0);
                else reg = src;
            } else {"""

new_node_var = """        case NODE_VAR: {
            int src = get_local_reg(n->name);
            if (src != -1) {
                if (reg != src) bc_emit_reg(compiling_bc, OP_MOVE, reg, src, 0);
            } else {"""

content = content.replace(old_node_var, new_node_var)

with open("src/compiler.c", "w") as f:
    f.write(content)
