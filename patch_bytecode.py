with open("src/glowlang.h", "r") as f:
    content = f.read()

content = content.replace("    int exception_count;\n};", "    int exception_count;\n    int preserved_locals;\n};")

with open("src/glowlang.h", "w") as f:
    f.write(content)

with open("src/compiler.c", "r") as f:
    content = f.read()

content = content.replace("bc->exception_count = 0;", "bc->exception_count = 0;\n    bc->preserved_locals = fe ? fe->param_count : 0;")

with open("src/compiler.c", "w") as f:
    f.write(content)

with open("src/vm.c", "r") as f:
    content = f.read()

content = content.replace("""    int param_count = (bc->owner) ? bc->owner->param_count : 0; \\
    int is_action = (bc->owner && bc->owner->local_count > 0 && bc->owner->local_names && strcmp(bc->owner->local_names[0], "self") == 0); \\
    int preserved = is_action ? param_count + 1 : param_count; \\
    for (int _i = preserved; _i < bc->local_count; _i++) vm_stack[base_reg + _i] = value_none(); \\""", 
"""    for (int _i = bc->preserved_locals; _i < bc->local_count; _i++) vm_stack[base_reg + _i] = value_none(); \\""")

content = content.replace("""    int param_count = (bc->owner) ? bc->owner->param_count : 0;
    bool is_action = (bc->owner && bc->owner->local_count > 0 && bc->owner->local_names && strcmp(bc->owner->local_names[0], "self") == 0);
    int preserved = is_action ? param_count + 1 : param_count;
    
    for (int i = preserved; i < bc->local_count; i++) {
        vm_stack[base_reg + i] = value_none();
    }""",
"""    for (int i = bc->preserved_locals; i < bc->local_count; i++) {
        vm_stack[base_reg + i] = value_none();
    }""")

with open("src/vm.c", "w") as f:
    f.write(content)
