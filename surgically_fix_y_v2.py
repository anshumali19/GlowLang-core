import os

def update():
    with open("src/glowlang.y", "r") as f:
        content = f.read()
    
    # 1. Fix jit_emit_byte arg
    content = content.replace('jit_emit_byte(buf, setcc)', 'jit_emit_byte(&buf, setcc)')

    # 2. Fix redefinitions in l_OP_CALL
    # The subagent might have duplicated blocks.
    # We'll just remove the duplicate lines if they exist.
    content = content.replace('int total = fe->param_count + fe->local_count; Value *saved = total > 0 ? malloc(sizeof(Value)*total) : NULL;\n            int total = fe->param_count + fe->local_count;', 'int total = fe->param_count + fe->local_count;')

    # 3. Add missing labels
    if 'l_OP_FUNC_DEF:' not in content:
        content = content.replace('l_OP_CALL:', 'l_OP_FUNC_DEF: pc++; DISPATCH();\n    l_OP_CALL:')
    
    if 'l_OP_SKIP:' not in content:
        content = content.replace('l_OP_STOP:', 'l_OP_SKIP: pc++; DISPATCH();\n    l_OP_STOP:')

    with open("src/glowlang.y", "w") as f:
        f.write(content)

if __name__ == "__main__":
    update()
