with open("src/vm.c", "r") as f:
    content = f.read()

old_macro = """#define DO_CALL(target_bc, target_closure, new_base_reg, target_ret_reg) do { \\
    if (call_depth + internal_depth >= 512) { pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW; } \\
    frames[internal_depth].bc = bc; \\
    frames[internal_depth].pc = pc; \\
    frames[internal_depth].closure = current_closure; \\
    frames[internal_depth].base_reg = base_reg; \\
    frames[internal_depth].ret_reg = target_ret_reg; \\
    frames[internal_depth].old_arena_used = old_arena_used; \\
    frames[internal_depth].my_frame_idx = my_frame_idx; \\
    frames[internal_depth].old_top = old_top; \\
    internal_depth++; \\
    call_stack_push((target_bc)->owner, (target_bc)->code); \\
    bc = (target_bc); \\
    pc = (target_bc)->code; \\
    current_closure = (target_closure); \\
    base_reg = (new_base_reg); \\"""

new_macro = """#define DO_CALL(target_bc, target_closure, new_base_reg, target_ret_reg) do { \\
    int _eval_new_base = (new_base_reg); \\
    if (call_depth + internal_depth >= 512) { pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW; } \\
    frames[internal_depth].bc = bc; \\
    frames[internal_depth].pc = pc; \\
    frames[internal_depth].closure = current_closure; \\
    frames[internal_depth].base_reg = base_reg; \\
    frames[internal_depth].ret_reg = target_ret_reg; \\
    frames[internal_depth].old_arena_used = old_arena_used; \\
    frames[internal_depth].my_frame_idx = my_frame_idx; \\
    frames[internal_depth].old_top = old_top; \\
    internal_depth++; \\
    call_stack_push((target_bc)->owner, (target_bc)->code); \\
    bc = (target_bc); \\
    pc = (target_bc)->code; \\
    current_closure = (target_closure); \\
    base_reg = _eval_new_base; \\"""

content = content.replace(old_macro, new_macro)
with open("src/vm.c", "w") as f:
    f.write(content)
