import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

jit_lt_ops = """            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
                emit_pop_r10(&buf); emit_pop_rax(&buf);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x10);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xf8); jit_emit_byte(&buf, 0x10);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xe2); jit_emit_byte(&buf, 0x10);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xfa); jit_emit_byte(&buf, 0x10);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xd0);
                uint8_t setcc = 0;
                if (pc->op == OP_LT) setcc = 0x9c; else if (pc->op == OP_GT) setcc = 0x9f;
                else if (pc->op == OP_LTE) setcc = 0x9e; else if (pc->op == OP_GTE) setcc = 0x9d;
                else if (pc->op == OP_EQ) setcc = 0x94; else if (pc->op == OP_NEQ) setcc = 0x95;
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, setcc); jit_emit_byte(&buf, 0xc0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xb6); jit_emit_byte(&buf, 0xc0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xbb); jit_emit_u64(&buf, 0x7FFA000000000000ULL);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd8);
                emit_push_rax(&buf); break;
            }"""

text = re.sub(r'case OP_LT: case OP_GT:.*?break;\s+\}', jit_lt_ops, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
