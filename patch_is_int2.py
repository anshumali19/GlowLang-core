with open("src/glowlang.h", "r") as f:
    content = f.read()

content = content.replace("#define GET_TYPE(v)", "#define IS_INT(v) (((v) & MASK_PAYLOAD) == (0x0002ULL << 48))\n#define GET_TYPE(v)")

with open("src/glowlang.h", "w") as f:
    f.write(content)
