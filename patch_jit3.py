with open("src/vm.c", "r") as f:
    content = f.read()

old_call = """    l_OP_CALL_SELF_FAST: {
        // int ac = pc->arg; 
        FuncEntry *fe = bc->owner;
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
            void *jit = func_get_specialization(fe, sig);
            if (jit && jit != (void*)1) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                JITFunc f = (JITFunc)jit; int deopt_pc = 0;
                Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            } else {
                DO_CALL(fe->body, NULL, base_reg + pc->r2, pc->r0);
            }
        }
        pc++; goto l_DISPATCH;
    }"""

new_call = """    l_OP_CALL_SELF_FAST: {
        FuncEntry *fe = bc->owner;
        if (fe && fe->body) {
            DO_CALL(fe->body, NULL, base_reg + pc->r2, pc->r0);
        }
        pc++; goto l_DISPATCH;
    }"""

content = content.replace(old_call, new_call)
with open("src/vm.c", "w") as f:
    f.write(content)
