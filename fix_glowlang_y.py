
import re

with open('src/glowlang.y', 'r') as f:
    content = f.read()

# 1. Restore Grammar
# Add RETURN, STOP, SKIP, FOR EACH to stmt rule
stmt_match = re.search(r'(stmt:.*?)(;)', content, re.DOTALL)
if stmt_match:
    stmt_body = stmt_match.group(1)
    if 'RETURN' not in stmt_body:
        stmt_body += '\n    | RETURN expr { $$ = newReturnNode($2); }'
    if 'STOP' not in stmt_body:
        stmt_body += '\n    | STOP { $$ = node_new(NODE_STOP); }'
    if 'SKIP' not in stmt_body:
        stmt_body += '\n    | SKIP { $$ = node_new(NODE_SKIP); }'
    if 'FOR EACH' not in stmt_body:
        stmt_body += '\n    | FOR EACH ID IN expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $3; $$->left = $5; $$->right = $8; }'
    
    content = content.replace(stmt_match.group(0), stmt_body + ';')

# 2. Fix JIT return value
# The interpreter part is already correct: l_OP_RETURN: return sp > 0 ? POP() : value_none();
# But let's check JIT OP_RETURN offsets
content = re.sub(
    r'case OP_RETURN:\s+jit_emit_byte\(&buf, 0x4d\); jit_emit_byte\(&buf, 0x85\); jit_emit_byte\(&buf, 0xed\); // test r13, r13\s+jit_emit_byte\(&buf, 0x74\); jit_emit_byte\(&buf, 13\); // jz return_none',
    'case OP_RETURN:\n                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xed); // test r13, r13\n                jit_emit_byte(&buf, 0x74); jit_emit_byte(&buf, 9); // jz return_none',
    content
)

# 3. JIT Robustness (already handled by default case, but let's ensure it's there)
# It is already there in the file I read.

# 4. NaN Boxing Fix for OP_ADD, OP_SUB, OP_MUL, OP_DIV
nan_boxing_logic = """            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV: {
                emit_pop_rbx(&buf); emit_pop_rax(&buf);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xb9); jit_emit_u64(&buf, 0x0000FFFFFFFFFFFFULL); // mov rcx, mask
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xc8); // and rax, rcx
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xcb); // and rbx, rcx
                if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xd8); }
                else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xd8); }
                else if (pc->op == OP_MUL) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc3); }
                else if (pc->op == OP_DIV) { 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x10); 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xf8); jit_emit_byte(&buf, 0x10);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xe3); jit_emit_byte(&buf, 0x10);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0xfb); jit_emit_byte(&buf, 0x10);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x99); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xf7); jit_emit_byte(&buf, 0xfb); 
                }
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xb9); jit_emit_u64(&buf, 0x0000FFFFFFFFFFFFULL);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xc8); // and rax, rcx
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0xb9); jit_emit_u64(&buf, 0x7FF8000000000000ULL);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); // or rax, rcx
                emit_push_rax(&buf);
                break;
            }"""

content = re.sub(
    r'case OP_ADD:.*?break;\s+}',
    nan_boxing_logic,
    content,
    flags=re.DOTALL
)

with open('src/glowlang.y', 'w') as f:
    f.write(content)
