import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Fix array allocation
alloc_old = r"uint32_t \*offsets = calloc\(bc->count, sizeof\(uint32_t\)\);"
alloc_new = "uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));"
text = re.sub(alloc_old, alloc_new, text)

# Add offsets[bc->count] = buf.size; before the jump patching loop
loop_old = r"    for \(int i = 0; i < bc->count; i\+\+\) \{ Instruction \*pc = &bc->code\[i\]; if \(pc->op == OP_JUMP \|\| pc->op == OP_JUMP_IF_FALSE\) \{ uint32_t rel = offsets\[pc->arg\] - \(jump_patches\[i\] \+ 4\); memcpy\(code \+ jump_patches\[i\], &rel, 4\); \} \}"
loop_new = """    offsets[bc->count] = buf.size;
    for (int i = 0; i < bc->count; i++) { Instruction *pc = &bc->code[i]; if (pc->op == OP_JUMP || pc->op == OP_JUMP_IF_FALSE) { uint32_t rel = offsets[pc->arg] - (jump_patches[i] + 4); memcpy(code + jump_patches[i], &rel, 4); } }"""
text = re.sub(loop_old, loop_new, text)

with open("src/glowlang.y", "w") as f:
    f.write(text)
