import os

def update():
    with open("src/glowlang.y", "r") as f:
        content = f.read()
    
    # 1. Add lsp.h include if missing
    if '#include "lsp.h"' not in content:
        content = content.replace('#include "glowlang.h"', '#include "glowlang.h"\n#include "lsp.h"')

    # 2. Update run_bytecode logic
    # We'll use a very specific replacement for l_OP_CALL
    old_call = """    l_OP_CALL: {
        int ac = pc->arg; FuncEntry *fe = func_get((const char*)pc->ptr);
        if (fe && fe->body) {"""
    
    new_call = """    l_OP_CALL: {
        int ac = pc->arg; FuncEntry *fe = func_get((const char*)pc->ptr);
        if (fe && fe->body) {
            int total = fe->param_count + fe->local_count;
            Value *saved = total > 0 ? malloc(sizeof(Value)*total) : NULL;
            for(int i=0; i<fe->param_count; i++) saved[i] = var_get(fe->params[i]);
            for(int i=0; i<fe->local_count; i++) saved[fe->param_count+i] = var_get(fe->local_names[i]);"""

    if old_call in content and new_call not in content:
        content = content.replace(old_call, new_call)

    # 3. Update restoration in l_OP_CALL
    old_restore = """            sp -= ac; Value res = run_bytecode(fe->body, tsp);
            if(has_exception) { PUSH(pending_exception); goto l_OP_THROW; } PUSH(res);"""
    
    new_restore = """            sp -= ac; Value res = run_bytecode(fe->body, tsp);
            for(int i=0; i<fe->param_count; i++) var_set(fe->params[i], saved[i]);
            for(int i=0; i<fe->local_count; i++) var_set(fe->local_names[i], saved[fe->param_count+i]);
            if(saved) free(saved);
            if(has_exception) { PUSH(pending_exception); goto l_OP_THROW; } PUSH(res);"""

    if old_restore in content and new_restore not in content:
        content = content.replace(old_restore, new_restore)

    # 4. Update main to handle --lsp
    old_main = """int main(int argc, char **argv) {
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    if (argc > 1) {"""
    
    new_main = """int main(int argc, char **argv) {
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    if (argc > 1) {
        if (strcmp(argv[1], "--lsp") == 0) { handle_client(0); return 0; }"""

    if old_main in content and new_main not in content:
        content = content.replace(old_main, new_main)

    with open("src/glowlang.y", "w") as f:
        f.write(content)

if __name__ == "__main__":
    update()
