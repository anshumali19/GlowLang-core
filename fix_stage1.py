import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Fix l_cv native function call safety
old_native_call = "else if(ot==OBJ_NATIVE_FUNC) { NativeFunc *nf = AS_NATIVE_FUNC(callee); PUSH(nf->func(ac, &vm_stack[sp-ac])); int _ac=ac; while(_ac--) POP(); }"
new_native_call = """else if(ot==OBJ_NATIVE_FUNC) {
                NativeFunc *nf = AS_NATIVE_FUNC(callee);
                Value *args = &vm_stack[sp - ac];
                Value res = nf->func(ac, args);
                sp -= ac;
                PUSH(res);
            }"""
text = text.replace(old_native_call, new_native_call)

# 2. Fix l_store to pop
text = text.replace("l_store: var_set((const char*)bc->code[ip].ptr, vm_stack[sp-1]); ip++; DISPATCH();", "l_store: var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();")

# 3. Add OP_LIST_LITERAL implementation (l_list)
list_impl = """l_list: {
        int n = bc->code[ip].arg;
        List *l = list_new(n);
        for (int i = 0; i < n; i++) list_push(l, vm_stack[sp - n + i]);
        sp -= n;
        PUSH(value_list(l));
        ip++; DISPATCH();
    }"""
# Find l_list in the big label list and replace it
text = text.replace("l_list:", list_impl)

# 4. Add OP_INDEX implementation (l_idx)
idx_impl = """l_idx: {
        Value idx = POP();
        Value obj = POP();
        if (GET_TYPE(obj) == VAL_LIST && GET_TYPE(idx) == VAL_INT) {
            List *l = AS_LIST(obj);
            int i = AS_INT(idx);
            if (i >= 0 && i < l->size) PUSH(l->items[i]);
            else PUSH(value_none());
        } else if (GET_TYPE(obj) == VAL_DICT) {
            PUSH(dict_get(AS_DICT(obj), AS_STRING(idx)));
        } else PUSH(value_none());
        ip++; DISPATCH();
    }"""
text = text.replace("l_idx:", idx_impl)

with open("src/glowlang.y", "w") as f:
    f.write(text)
