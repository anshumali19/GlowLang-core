with open("src/vm.c", "r") as f:
    content = f.read()

content = content.replace("if (call_depth + internal_depth >= 512)", "if (__builtin_expect(call_depth + internal_depth >= 512, 0))")
content = content.replace("if (has_exception)", "if (__builtin_expect(has_exception, 0))")
content = content.replace("if (IS_DEOPT(res))", "if (__builtin_expect(IS_DEOPT(res), 0))")
content = content.replace("if (base_reg + bc->local_count >= vm_stack_size)", "if (__builtin_expect(base_reg + bc->local_count >= vm_stack_size, 0))")
content = content.replace("if (base_reg + bc->local_count > vm_stack_top)", "if (__builtin_expect(base_reg + bc->local_count > vm_stack_top, 0))")

with open("src/vm.c", "w") as f:
    f.write(content)
