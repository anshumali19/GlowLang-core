import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Remove dangerous variable caching (it breaks global var updates)
load_var_old = r"l_load: \{.*?ip\+\+; DISPATCH\(\);\s+\}"
load_var_new = """l_load: {
        const char *name = (const char*)bc->code[ip].ptr;
        Value v = var_get(name);
        if (GET_TYPE(v) == VAL_NONE) {
            FuncEntry *fe = func_get(name);
            if (fe) v = bound_method_new(value_none(), fe);
        }
        PUSH(v); ip++; DISPATCH();
    }"""
text = re.sub(load_var_old, load_var_new, text, flags=re.DOTALL)

# 2. Fix l_call and l_call_value to be safer
call_fix = """l_call: {
        int ac = bc->code[ip].arg; const char *n = (const char*)bc->code[ip].ptr; FuncEntry *fe = func_get(n);
        if (fe && fe->body) {
            int pc = fe->param_count;
            Value *saved = pc > 0 ? malloc(sizeof(Value) * pc) : NULL;
            for (int i = 0; i < pc; i++) {
                saved[i] = var_get(fe->params[i]);
                if (i < ac) var_set(fe->params[i], vm_stack[sp - ac + i]);
            }
            sp -= ac; Value res = run_bytecode(fe->body, 0);
            for (int i = 0; i < pc; i++) var_set(fe->params[i], saved[i]);
            if (saved) free(saved);
            if (has_exception) { PUSH(pending_exception); goto l_throw; }
            PUSH(res);
        } else { sp -= ac; PUSH(value_none()); }
        ip++; DISPATCH();
    }"""
text = re.sub(r"l_call: \{.*?ip\+\+; DISPATCH\(\);\s+\}", call_fix, text, flags=re.DOTALL)

# 3. Ensure l_throw is robust
throw_fix = """l_throw: {
        Value exc = (sp > 0) ? POP() : pending_exception;
        if (tsp > 0) {
            TryHandler h = try_stack[--tsp];
            sp = h.sp;
            PUSH(exc);
            has_exception = 0;
            ip = h.handler_ip;
            DISPATCH();
        }
        pending_exception = exc;
        has_exception = 1;
        return value_none();
    }"""
text = re.sub(r"l_throw: \{.*?\}", throw_fix, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
