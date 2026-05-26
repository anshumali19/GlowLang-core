import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Fix Prologue (Add push r15 for 16-byte stack alignment)
prologue_old = r"jit_emit_byte\(&buf, 0x55\); jit_emit_byte\(&buf, 0x48\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0xe5\);\s*jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x54\); jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x55\); jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x56\);"
prologue_new = """jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5);
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x54); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x56); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x57); // push r15 for alignment"""
text = re.sub(prologue_old, prologue_new, text)

# Fix Epilogue in OP_RETURN (Add pop r15)
epilogue_old = r"jit_emit_byte\(&buf, 0x45\); jit_emit_byte\(&buf, 0x89\); jit_emit_byte\(&buf, 0x2e\); jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x5e\); jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x5d\); jit_emit_byte\(&buf, 0x41\); jit_emit_byte\(&buf, 0x5c\); jit_emit_byte\(&buf, 0x5d\); jit_emit_byte\(&buf, 0xc3\);"
epilogue_new = """jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x2e); 
                jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); // pop r15
                jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);"""
text = re.sub(epilogue_old, epilogue_new, text)

with open("src/glowlang.y", "w") as f:
    f.write(text)
