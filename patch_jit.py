with open("src/vm.c", "r") as f:
    content = f.read()

old_resume = """    Instruction *pc = start_pc ? start_pc : bc->code;

    if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
        bc->code[0].exec_count++;
        if (1) jit_compile(bc);
    }
    if (!start_pc && bc->jit_code) {
        typedef Value (*JITFunc)(Value *R, int *pc_idx);
        int deopt_pc = 0;
        Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
        if (IS_DEOPT(res)) {
            pc = &bc->code[deopt_pc];
            goto l_RESUME_INTERP;
        }
        call_stack_pop();
        vm_stack_top = old_top;
        gc_free_stack_arena_range(old_arena_used, stack_arena_used);
        stack_arena_used = old_arena_used;
        call_depth--;
        return res;
    }

    l_RESUME_INTERP:;"""

new_resume = """    Instruction *pc = start_pc ? start_pc : bc->code;

    l_RESUME_INTERP:;
    
    if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
        if (++bc->code[0].exec_count > 10) {
            if (1) jit_compile(bc);
        }
    }
    if (!start_pc && bc->jit_code) {
        typedef Value (*JITFunc)(Value *R, int *pc_idx);
        int deopt_pc = 0;
        Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
        if (IS_DEOPT(res)) {
            pc = &bc->code[deopt_pc];
        } else {
            Value ret_val = res;
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
    }"""

content = content.replace(old_resume, new_resume)
with open("src/vm.c", "w") as f:
    f.write(content)
