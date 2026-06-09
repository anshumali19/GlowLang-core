import re

with open('src/vm_new.c', 'r') as f:
    content = f.read()

do_call_macro = """
    typedef struct {
        Bytecode *bc;
        Instruction *pc;
        Closure *closure;
        int base_reg;
        int ret_reg;
        size_t old_arena_used;
        int my_frame_idx;
        int old_top;
    } VMFrame;
    VMFrame frames[512];
    int internal_depth = 0;

#define DO_CALL(target_bc, target_closure, new_base_reg, target_ret_reg) do { \\
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
    base_reg = (new_base_reg); \\
    my_frame_idx = call_stack_ptr - 1; \\
    old_arena_used = stack_arena_used; \\
    old_top = vm_stack_top; \\
    if (base_reg + bc->local_count >= vm_stack_size) { \\
        int old_size = vm_stack_size; \\
        while (base_reg + bc->local_count >= vm_stack_size) vm_stack_size *= 2; \\
        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size); \\
        for (int _i=old_size; _i<vm_stack_size; _i++) vm_stack[_i] = 0; \\
    } \\
    if (base_reg + bc->local_count > vm_stack_top) vm_stack_top = base_reg + bc->local_count; \\
    int param_count = (bc->owner) ? bc->owner->param_count : 0; \\
    int is_action = (bc->owner && bc->owner->local_count > 0 && bc->owner->local_names && strcmp(bc->owner->local_names[0], "self") == 0); \\
    int preserved = is_action ? param_count + 1 : param_count; \\
    for (int _i = preserved; _i < bc->local_count; _i++) vm_stack[base_reg + _i] = value_none(); \\
    goto l_RESUME_INTERP; \\
} while(0)
"""

# Insert macro at the start of run_bytecode
content = content.replace("    Instruction *pc = start_pc ? start_pc : bc->code;", do_call_macro + "\n    Instruction *pc = start_pc ? start_pc : bc->code;")

# Modify l_OP_RETURN
ret_code = """
    l_OP_RETURN: {
        Value ret_val = R(pc->r0);
        if (internal_depth == 0) {
            return ret_val;
        } else {
            call_stack_pop();
            gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            vm_stack_top = old_top;
            
            internal_depth--;
            bc = frames[internal_depth].bc;
            pc = frames[internal_depth].pc;
            current_closure = frames[internal_depth].closure;
            base_reg = frames[internal_depth].base_reg;
            int ret_reg = frames[internal_depth].ret_reg;
            old_arena_used = frames[internal_depth].old_arena_used;
            my_frame_idx = frames[internal_depth].my_frame_idx;
            old_top = frames[internal_depth].old_top;
            
            if (ret_reg >= 0) R(ret_reg) = ret_val;
            pc++; DISPATCH();
        }
    }
"""

content = re.sub(r'    l_OP_RETURN: \{.*?\n    \}', ret_code, content, flags=re.DOTALL)

# Modify run_bytecode(ife->body, NULL, NULL, callee_base) to DO_CALL in l_OP_CALL_VALUE etc
content = content.replace('run_bytecode(ife->body, NULL, NULL, callee_base);', 
                          'DO_CALL(ife->body, NULL, callee_base, -1);')

content = content.replace('Value res = run_bytecode(bm->method->body, NULL, NULL, callee_base);\n                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(bm->method->body, NULL, callee_base, pc->r0);')

content = content.replace('Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);\n                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);')
                          
# For l_OP_CALL_METHOD and l_OP_CALL and others:
content = content.replace('Value res = run_bytecode(fe->body, NULL, NULL, callee_base);\n            if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(fe->body, NULL, callee_base, pc->r0);')

# OP_CALL_SELF_FAST
content = content.replace('Value res = run_bytecode(bc, NULL, current_closure, callee_base);\n        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(bc, current_closure, callee_base, pc->r0);')

# OP_SPAWN (Thread creation - should NOT be inlined, it calls run_bytecode recursively on new thread anyway... wait OP_SPAWN is `run_bytecode` on NEW thread, so leave it alone)
# Let's check OP_SPAWN to make sure it wasn't affected by above replacements

# Modify l_OP_THROW to unwind inlined frames
throw_code = """
    l_OP_THROW: {
        Value exc = (has_exception) ? pending_exception : R(pc->r0);
        pending_exception = exc; has_exception = 1;
        call_stack[my_frame_idx].pc = pc; // Record error location
        
        if (!exception_stack_trace) {
            exception_stack_trace = format_stack_trace();
            error_file = pc->file;
            error_line = pc->line;
        }

        int current_pc_idx = (int)(pc - bc->code);
        for (int i = 0; i < bc->exception_count; i++) {
            if (current_pc_idx >= bc->exception_table[i].start_pc && 
                current_pc_idx < bc->exception_table[i].end_pc) {
                R(0) = exc;
                has_exception = 0;
                pending_exception = 0;
                if (exception_stack_trace) { free(exception_stack_trace); exception_stack_trace = NULL; }
                pc = bc->code + bc->exception_table[i].handler_pc;
                DISPATCH();
            }
        }
        
        // If we reach here, no exception handler was found in the current frame.
        if (internal_depth > 0) {
            call_stack_pop();
            gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            vm_stack_top = old_top;
            
            internal_depth--;
            bc = frames[internal_depth].bc;
            pc = frames[internal_depth].pc;
            current_closure = frames[internal_depth].closure;
            base_reg = frames[internal_depth].base_reg;
            old_arena_used = frames[internal_depth].old_arena_used;
            my_frame_idx = frames[internal_depth].my_frame_idx;
            old_top = frames[internal_depth].old_top;
            goto l_OP_THROW; // Re-evaluate in caller
        }
        
        goto l_FINISH;
    }
"""

content = re.sub(r'    l_OP_THROW: \{.*?\n        goto l_FINISH;\n    \}', throw_code, content, flags=re.DOTALL)

with open('src/vm_new.c', 'w') as f:
    f.write(content)

