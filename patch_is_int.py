with open("src/glowlang.h", "r") as f:
    content = f.read()

content = content.replace("#define IS_INT(v)     (GET_TYPE(v) == VAL_INT)", "#define IS_INT(v)     ((v & MASK_PAYLOAD) == (0x0002ULL << 48))")

with open("src/glowlang.h", "w") as f:
    f.write(content)

with open("src/vm.c", "r") as f:
    content = f.read()

content = content.replace("GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT", "IS_INT(a) && IS_INT(b)")

with open("src/vm.c", "w") as f:
    f.write(content)
