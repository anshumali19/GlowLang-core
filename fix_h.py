import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Remove variable caching
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

store_var_old = r"l_store: \{.*?ip\+\+; DISPATCH\(\);\s+\}"
store_var_new = """l_store: {
        const char *name = (const char*)bc->code[ip].ptr;
        var_set(name, POP()); ip++; DISPATCH();
    }"""
text = re.sub(store_var_old, store_var_new, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
