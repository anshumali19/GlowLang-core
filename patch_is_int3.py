with open("src/glowlang.h", "r") as f:
    content = f.read()

content = content.replace("static inline ValueType GET_TYPE(Value v) {", "#define IS_INT(v) (((v) & 0xFFFF000000000000ULL) == 0x7FFA000000000000ULL)\nstatic inline ValueType GET_TYPE(Value v) {")

with open("src/glowlang.h", "w") as f:
    f.write(content)
