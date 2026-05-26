import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Replace the switch in jit_compile to pre-populate caches
new_cases = """        switch (pc->op) {
            case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_push_rax(&buf); break;
            case OP_CONST_BOOL: emit_mov_rax_imm64(&buf, value_bool(pc->arg)); emit_push_rax(&buf); break;
            case OP_LOAD_VAR: 
            case OP_STORE_VAR: {
                if (!pc->cache_val) {
                    const char *name = (const char*)pc->ptr;
                    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
                    unsigned int idx = h;
                    Value *vptr = NULL;
                    do {
                        if (!var_table[idx].used || strcmp(var_table[idx].name, name) == 0) {
                            if (!var_table[idx].used) { var_table[idx].used = 1; var_table[idx].name = name; var_table[idx].value = value_none(); }
                            vptr = &var_table[idx].value; break;
                        }
                        idx = (idx + 1) % VAR_TABLE_SIZE;
                    } while (idx != h);
                    pc->cache_key = (void*)name;
                    pc->cache_val = (intptr_t)vptr;
                }
                if (pc->op == OP_LOAD_VAR) {
                    emit_mov_rax_imm64(&buf, pc->cache_val); 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x00); 
                    emit_push_rax(&buf); 
                } else {
                    emit_pop_rax(&buf);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xba); jit_emit_u64(&buf, pc->cache_val); 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x02); 
                }
                break;
            }"""

text = re.sub(r'switch \(pc->op\) \{.*?case OP_STORE_VAR:.*?break;\s+\}', new_cases, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
