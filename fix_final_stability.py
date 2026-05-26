import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Initialize VM locals
init_locals = """
    int ip = 0; 
    Class *vm_last_class = NULL; 
    Trait *vm_last_trait = NULL;
    #define DISPATCH() goto *dt[bc->code[ip].op]
"""
text = text.replace("int ip = 0; #define DISPATCH() goto *dt[bc->code[ip].op]", init_locals)

# 2. Fix AS_TRAIT missing in some contexts? No, it's in .h

# 3. Add proper thread_worker for spawn
worker = """
static void* thread_worker(void *arg) {
    ThreadData *td = (ThreadData*)arg;
    vm_stack_init();
    pthread_mutex_lock(&glow_gil);
    if (GET_TYPE(td->callee) == VAL_CLOSURE) {
        run_bytecode(AS_CLOSURE(td->callee)->body, 0);
    } else if (GET_TYPE(td->callee) == VAL_OBJECT && GET_OBJ_TYPE(td->callee) == OBJ_BOUND_METHOD) {
        BoundMethod *bm = AS_BOUND_METHOD(td->callee);
        var_set("self", bm->instance);
        run_bytecode(bm->method->body, 0);
    }
    pthread_mutex_unlock(&glow_gil);
    free(td->args); free(td);
    return NULL;
}
"""
text = re.sub(r"static void\* thread_worker\(void \*arg\) \{.*?\}", worker, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
