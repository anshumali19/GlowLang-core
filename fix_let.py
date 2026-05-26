import re
with open('src/glowlang.y', 'r') as f:
    text = f.read()

text = text.replace('l_OP_STORE_LET: pc++; DISPATCH();', 'l_OP_STORE_LET: var_set((const char*)pc->ptr, POP()); pc++; DISPATCH();')
with open('src/glowlang.y', 'w') as f:
    f.write(text)
