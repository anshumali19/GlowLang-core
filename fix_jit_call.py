import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Fix JUMP_IF_FALSE stack alignment 
jump_if_false_old = r"case OP_JUMP_IF_FALSE: \{.*?break;\s+\}"
jump_if_false_new = """            case OP_JUMP_IF_FALSE: {
                emit_pop_rax(&buf);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                
                // Align stack to 16 bytes before C call
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe0); // mov rax, rsp
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x24); jit_emit_byte(&buf, 0xf0); // and al, 0xf0 (align 16)
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc4); // sub rsp, rax
                jit_emit_byte(&buf, 0x50); // push rax (save adjustment)

                emit_mov_rax_imm64(&buf, (uintptr_t)value_truthy);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0); // call rax (which is value_truthy)
                
                // Restore stack
                jit_emit_byte(&buf, 0x5b); // pop rbx (adjustment)
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xdc); // add rsp, rbx

                jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); // test eax, eax
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jump_patches[i] = buf.size; jit_emit_u32(&buf, 0); // jz ...
                break;
            }"""

text = re.sub(jump_if_false_old, jump_if_false_new, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
