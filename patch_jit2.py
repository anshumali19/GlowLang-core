with open("src/vm.c", "r") as f:
    content = f.read()

content = content.replace("if (ret_reg >= 0) R(ret_reg) = ret_val;", "if (ret_reg >= 0) vm_stack[base_reg + ret_reg] = ret_val;")
content = content.replace("pc++; DISPATCH();", "pc++; goto l_DISPATCH;")
content = content.replace("    DISPATCH();\n\n    l_OP_CONST_INT:", "    l_DISPATCH: DISPATCH();\n\n    l_OP_CONST_INT:")

with open("src/vm.c", "w") as f:
    f.write(content)
