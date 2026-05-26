import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Fix OP_LOAD_VAR C call
load_var_c_call = """// C call wrapper for var_get
                jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe4); jit_emit_byte(&buf, 0xf0);
                emit_mov_rax_imm64(&buf, (uintptr_t)pc->ptr); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); emit_mov_rax_imm64(&buf, (uintptr_t)var_get); jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xec); jit_emit_byte(&buf, 0x5d);"""
text = re.sub(
    r'emit_mov_rax_imm64\(&buf, \(uintptr_t\)pc->ptr\); jit_emit_byte\(&buf, 0x48\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0xc7\); emit_mov_rax_imm64\(&buf, \(uintptr_t\)var_get\); jit_emit_byte\(&buf, 0xff\); jit_emit_byte\(&buf, 0xd0\);',
    load_var_c_call,
    text
)

# Fix OP_STORE_VAR C call
store_var_c_call = """// C call wrapper for var_set
                jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe4); jit_emit_byte(&buf, 0xf0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc6); emit_mov_rax_imm64(&buf, (uintptr_t)pc->ptr); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); emit_mov_rax_imm64(&buf, (uintptr_t)var_set); jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xec); jit_emit_byte(&buf, 0x5d);"""
text = re.sub(
    r'jit_emit_byte\(&buf, 0x48\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0xc6\); emit_mov_rax_imm64\(&buf, \(uintptr_t\)pc->ptr\); jit_emit_byte\(&buf, 0x48\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0xc7\); emit_mov_rax_imm64\(&buf, \(uintptr_t\)var_set\); jit_emit_byte\(&buf, 0xff\); jit_emit_byte\(&buf, 0xd0\);',
    store_var_c_call,
    text
)

# Fix OP_JUMP_IF_FALSE C call
jump_if_false_c_call = """// C call wrapper for value_truthy
                jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe4); jit_emit_byte(&buf, 0xf0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); emit_mov_rax_imm64(&buf, (uintptr_t)value_truthy); jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xec); jit_emit_byte(&buf, 0x5d);"""
text = re.sub(
    r'jit_emit_byte\(&buf, 0x48\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0xc7\); emit_mov_rax_imm64\(&buf, \(uintptr_t\)value_truthy\); jit_emit_byte\(&buf, 0xff\); jit_emit_byte\(&buf, 0xd0\);',
    jump_if_false_c_call,
    text
)

with open("src/glowlang.y", "w") as f:
    f.write(text)
