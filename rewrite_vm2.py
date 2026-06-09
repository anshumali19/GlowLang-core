with open('src/vm_new.c', 'r') as f:
    content = f.read()

# OP_CALL
content = content.replace('Value res = run_bytecode(fe->body, NULL, NULL, base_reg + pc->r2);\n                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(fe->body, NULL, base_reg + pc->r2, pc->r0);')

# fallback_callee in OP_CALL_METHOD for closure
content = content.replace('Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);\n                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);')

# fallback_callee in OP_CALL_METHOD for bound method
content = content.replace('Value res = run_bytecode(bm->method->body, NULL, NULL, base_reg + pc->r2);\n                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;',
                          'DO_CALL(bm->method->body, NULL, base_reg + pc->r2, pc->r0);')

with open('src/vm_new.c', 'w') as f:
    f.write(content)
