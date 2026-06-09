    l_OP_RETURN: {
        Value ret_val = R(pc->r0);
        if (internal_depth == 0) {
            R(0) = ret_val;
            goto l_FINISH;
        } else {
            call_stack_pop();
            if (stack_arena_used > old_arena_used) gc_free_stack_arena_range(old_arena_used, stack_arena_used);
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
            
            if (ret_reg >= 0) vm_stack[base_reg + ret_reg] = ret_val;
            pc++; goto l_DISPATCH;
        }
    }
