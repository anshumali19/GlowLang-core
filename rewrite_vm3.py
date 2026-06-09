with open('src/vm_new.c', 'r') as f:
    content = f.read()

content = content.replace('Value res = run_bytecode(bm->method->body, NULL, NULL, base_reg + pc->r2);\n                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(bm->method->body, NULL, base_reg + pc->r2, pc->r0);')

content = content.replace('Value res = run_bytecode(bm->method->body, NULL, NULL, callee_base);\n                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(bm->method->body, NULL, callee_base, pc->r0);')

with open('src/vm_new.c', 'w') as f:
    f.write(content)
