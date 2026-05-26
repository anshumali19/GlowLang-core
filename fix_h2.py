import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Add os_sleep
sleep_impl = """
static Value os_sleep(int argc, Value *args) {
    if (argc > 0) {
        pthread_mutex_unlock(&glow_gil);
        usleep(AS_INT(args[0]) * 1000);
        pthread_mutex_lock(&glow_gil);
    }
    return value_none();
}
"""
text = text.replace("static Value os_system", sleep_impl + "static Value os_system")
text = text.replace('dict_set(os, "system", native_func_new("system", os_system));', 'dict_set(os, "system", native_func_new("system", os_system));\\n    dict_set(os, "sleep", native_func_new("sleep", os_sleep));')

# 2. Add GIL release in long loops (JUMP instruction)
# Every time we jump backwards, we release and re-acquire the GIL to let other threads run.
jump_gil = """l_jump: {
        int target = bc->code[ip].arg;
        if (target <= ip) { // Backward jump
            pthread_mutex_unlock(&glow_gil);
            sched_yield();
            pthread_mutex_lock(&glow_gil);
        }
        ip = target; DISPATCH();
    }"""
text = text.replace("l_jump: ip = bc->code[ip].arg; DISPATCH();", jump_gil)

with open("src/glowlang.y", "w") as f:
    f.write(text)
