import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Fix tokens in %token list
text = text.replace("%token <int_val> INT_LIT", "%token <int_val> INT_LIT\n%token <float_val> FLOAT_LIT")
text = text.replace("TRUE_KW FALSE_KW", "TRUE_KW FALSE_KW AND_OP OR_OP NOT_OP")

# 2. Fix static vs extern
text = text.replace("static Node* node_new", "Node* node_new")
text = text.replace("static void compile", "void compile")

# 3. Fix DISPATCH macro location
text = text.replace("int ip = 0; #define DISPATCH()", "#define DISPATCH() goto *dt[bc->code[ip].op]\\n    int ip = 0;")

with open("src/glowlang.y", "w") as f:
    f.write(text)
