with open("src/vm.c", "r") as f:
    content = f.read()

content = content.replace("            gc_free_stack_arena_range(old_arena_used, stack_arena_used);",
                          "            if (stack_arena_used > old_arena_used) gc_free_stack_arena_range(old_arena_used, stack_arena_used);")

with open("src/vm.c", "w") as f:
    f.write(content)
