import re

with open("src/glowlang.y", "r") as f:
    content = f.read()

all_ops = [
    "OP_CONST_INT", "OP_CONST_FLOAT", "OP_CONST_STRING", "OP_CONST_BOOL",
    "OP_LOAD_VAR", "OP_STORE_VAR", "OP_ADD", "OP_SUB", "OP_MUL", "OP_DIV",
    "OP_MOD", "OP_POW", "OP_NEG", "OP_LT", "OP_GT", "OP_LTE", "OP_GTE",
    "OP_EQ", "OP_NEQ", "OP_AND", "OP_OR", "OP_NOT", "OP_STRING_CONCAT",
    "OP_JUMP", "OP_JUMP_IF_FALSE", "OP_JUMP_IF_TRUE", "OP_LEN", "OP_RANGE",
    "OP_TYPE", "OP_APPEND", "OP_INPUT", "OP_SHOW", "OP_WRITE", "OP_LIST_LITERAL",
    "OP_DICT_LITERAL", "OP_INDEX", "OP_SLICE", "OP_STORE_INDEX", "OP_FUNC_DEF",
    "OP_CALL", "OP_RETURN", "OP_FOR_EACH", "OP_FOR_EACH_PAIR", "OP_STOP",
    "OP_SKIP", "OP_POP", "OP_PUSH_SCOPE", "OP_POP_SCOPE", "OP_STORE_LET",
    "OP_LOAD_LOCAL", "OP_STORE_LOCAL", "OP_DICT_KEYS", "OP_DICT_VALUES",
    "OP_DICT_HAS", "OP_DICT_REMOVE", "OP_DICT_MERGE", "OP_MATH_ABS",
    "OP_MATH_MIN", "OP_MATH_MAX", "OP_MATH_ROUND", "OP_MATH_FLOOR",
    "OP_MATH_CEIL", "OP_MATH_SQRT", "OP_STR_UPPER", "OP_STR_LOWER",
    "OP_STR_TRIM", "OP_STR_FIND", "OP_STR_REPLACE", "OP_STR_STARTSWITH",
    "OP_STR_ENDSWITH", "OP_STR_SPLIT", "OP_LIST_PUSH", "OP_LIST_POP",
    "OP_LIST_INSERT", "OP_LIST_REMOVE", "OP_LIST_CONTAINS", "OP_LIST_SORT",
    "OP_LIST_REVERSE", "OP_FILE_READ", "OP_FILE_WRITE", "OP_FILE_APPEND",
    "OP_FSTRING", "OP_LIST_COMP", "OP_THROW", "OP_TRY_START", "OP_TRY_END",
    "OP_LAMBDA", "OP_CALL_VALUE", "OP_CLASS_DEF", "OP_METHOD_DEF",
    "OP_TRAIT_DEF", "OP_USE_TRAIT", "OP_GET_PROPERTY", "OP_SET_PROPERTY",
    "OP_SPAWN", "OP_BUILTIN_SHOW"
]

new_run_bytecode = r"""static Value run_bytecode(Bytecode *bc, int base_tsp) {
    if (bc->count > 0 && !bc->jit_code && !bc->jit_failed) { bc->code[0].exec_count++; if (bc->code[0].exec_count >= 1) jit_compile(bc); }
    if (bc->jit_code) { typedef Value (*JitFunc)(Value *stack, int *sp_ptr); return ((JitFunc)bc->jit_code)(vm_stack, &sp); }
    Instruction *pc = bc->code;
    static void* dt[] = {
"""
for op in all_ops:
    new_run_bytecode += f"        &&l_{op},\n"
new_run_bytecode += r"""    };
#define DISPATCH() goto *dt[pc->op]
    DISPATCH();

    l_OP_CONST_INT: PUSH(value_int(pc->arg)); pc++; DISPATCH();
    l_OP_CONST_FLOAT: PUSH(*(Value*)pc->ptr); pc++; DISPATCH();
    l_OP_CONST_STRING: PUSH(value_string_copy((const char*)pc->ptr)); pc++; DISPATCH();
    l_OP_CONST_BOOL: PUSH(value_bool(pc->arg)); pc++; DISPATCH();
    l_OP_LOAD_VAR: {
        const char *name = (const char*)pc->ptr;
        unsigned int h = hash_str(name) % VAR_TABLE_SIZE; unsigned int idx = h; Value *vptr = NULL;
        do { if (!var_table[idx].used || strcmp(var_table[idx].name, name) == 0) { if (!var_table[idx].used) { var_table[idx].used = 1; var_table[idx].name = name; var_table[idx].value = value_none(); } vptr = &var_table[idx].value; break; } idx = (idx + 1) % VAR_TABLE_SIZE; } while (idx != h);
        PUSH(*vptr); pc++; DISPATCH();
    }
    l_OP_STORE_VAR: { var_set((const char*)pc->ptr, POP()); pc++; DISPATCH(); }
    l_OP_ADD: {
        Value b = POP(), a = POP();
        if (__builtin_expect(GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT, 1)) {
            PUSH(value_int(AS_INT(a) + AS_INT(b)));
        } else if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
            char buf1[64], buf2[64]; const char *s1, *s2;
            if (GET_TYPE(a) == VAL_STRING) s1 = AS_STRING(a); else if(GET_TYPE(a)==VAL_INT) sprintf(buf1, "%ld", (long)AS_INT(a)), s1 = buf1; else s1="none";
            if (GET_TYPE(b) == VAL_STRING) s2 = AS_STRING(b); else if(GET_TYPE(b)==VAL_INT) sprintf(buf2, "%ld", (long)AS_INT(b)), s2 = buf2; else s2="none";
            char *res = malloc(strlen(s1) + strlen(s2) + 1); strcpy(res, s1); strcat(res, s2);
            PUSH(value_string_copy(res)); free(res);
        } else PUSH(value_float(((GET_TYPE(a)==VAL_INT)?(double)AS_INT(a):AS_FLOAT(a)) + ((GET_TYPE(b)==VAL_INT)?(double)AS_INT(b):AS_FLOAT(b))));
        pc++; DISPATCH();
    }
    l_OP_SUB: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) - AS_INT(b))); pc++; DISPATCH(); }
    l_OP_MUL: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) * AS_INT(b))); pc++; DISPATCH(); }
    l_OP_DIV: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) / AS_INT(b))); pc++; DISPATCH(); }
    l_OP_MOD: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) % AS_INT(b))); pc++; DISPATCH(); }
    l_OP_POW: { Value b = POP(), a = POP(); PUSH(value_float(pow(AS_FLOAT(a), AS_FLOAT(b)))); pc++; DISPATCH(); }
    l_OP_NEG: { Value a = POP(); PUSH(value_int(-AS_INT(a))); pc++; DISPATCH(); }
    l_OP_LT: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) < AS_INT(b))); pc++; DISPATCH(); }
    l_OP_GT: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) > AS_INT(b))); pc++; DISPATCH(); }
    l_OP_LTE: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) <= AS_INT(b))); pc++; DISPATCH(); }
    l_OP_GTE: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) >= AS_INT(b))); pc++; DISPATCH(); }
    l_OP_EQ: { Value b = POP(), a = POP(); PUSH(value_bool(a == b)); pc++; DISPATCH(); }
    l_OP_NEQ: { Value b = POP(), a = POP(); PUSH(value_bool(a != b)); pc++; DISPATCH(); }
    l_OP_AND: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) && value_truthy(b))); pc++; DISPATCH(); }
    l_OP_OR: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) || value_truthy(b))); pc++; DISPATCH(); }
    l_OP_NOT: { Value a = POP(); PUSH(value_bool(!value_truthy(a))); pc++; DISPATCH(); }
    l_OP_STRING_CONCAT: pc++; DISPATCH();
    l_OP_JUMP: { pc = bc->code + pc->arg; DISPATCH(); }
    l_OP_JUMP_IF_FALSE: { Value v = POP(); if(!value_truthy(v)) pc = bc->code + pc->arg; else pc++; DISPATCH(); }
    l_OP_JUMP_IF_TRUE: { Value v = POP(); if(value_truthy(v)) pc = bc->code + pc->arg; else pc++; DISPATCH(); }
    l_OP_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr; FuncEntry *fe = func_get(n);
        if (fe && fe->body) {
            int total = fe->param_count + fe->local_count;
            Value *saved = total > 0 ? malloc(sizeof(Value)*total) : NULL;
            for(int i=0; i<fe->param_count; i++) saved[i] = var_get(fe->params[i]);
            for(int i=0; i<fe->local_count; i++) saved[fe->param_count+i] = var_get(fe->local_names[i]);
            for (int i=0; i<fe->param_count && i<ac; i++) var_set(fe->params[i], vm_stack[sp-ac+i]);
            sp -= ac; Value res = run_bytecode(fe->body, tsp);
            for(int i=0; i<fe->param_count; i++) var_set(fe->params[i], saved[i]);
            for(int i=0; i<fe->local_count; i++) var_set(fe->local_names[i], saved[fe->param_count+i]);
            if(saved) free(saved); if(has_exception) { PUSH(pending_exception); goto l_OP_THROW; } PUSH(res);
        } else { sp -= ac; PUSH(value_none()); }
        pc++; DISPATCH();
    }
    l_OP_RETURN: return sp > 0 ? POP() : value_none();
    l_OP_POP: if(sp > 0) POP(); pc++; DISPATCH();
    l_OP_SHOW:
    l_OP_BUILTIN_SHOW: { int n = pc->arg; for (int i=0; i<n; i++) { print_value(vm_stack[sp-n+i]); if(i<n-1) printf(" "); } printf("\n"); fflush(stdout); sp-=n; pc++; DISPATCH(); }
    l_OP_STORE_LET: { var_set((const char*)pc->ptr, POP()); pc++; DISPATCH(); }
    l_OP_TRY_START: try_stack[tsp++] = (TryHandler){ pc->arg, sp }; pc++; DISPATCH();
    l_OP_TRY_END: tsp--; pc++; DISPATCH();
    l_OP_THROW: {
        Value exc = (sp > 0) ? POP() : pending_exception;
        if (tsp > base_tsp) { TryHandler h = try_stack[--tsp]; sp = h.sp; PUSH(exc); has_exception = 0; pc = bc->code + h.handler_ip; DISPATCH(); }
        pending_exception = exc; has_exception = 1; return value_none();
    }
    l_OP_CLASS_DEF: {
        const char *n = (const char*)pc->ptr; Class *p = NULL;
        if (pc->arg == 1) { Value pv = POP(); if (GET_TYPE(pv) == VAL_OBJECT && (pv & MASK_PAYLOAD)) p = AS_CLASS(pv); }
        Value c = value_class(class_new(n, p)); var_set(n, c); vm_last_class = AS_CLASS(c); pc++; DISPATCH();
    }
    l_OP_TRAIT_DEF: { const char *n = (const char*)pc->ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; DISPATCH(); }
    l_OP_USE_TRAIT: {
        Value tv = var_get((const char*)pc->ptr);
        if (GET_TYPE(tv) == VAL_OBJECT && (tv & MASK_PAYLOAD) && GET_OBJ_TYPE(tv) == OBJ_TRAIT && vm_last_class) {
            Trait *t = AS_TRAIT(tv);
            for (int i=0; i<t->methods->size; i++) dict_set(vm_last_class->methods, t->methods->keys[i], t->methods->values[i]);
        }
        pc++; DISPATCH();
    }
    l_OP_GET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = POP();
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Value v = instance_get(AS_INSTANCE(o), n);
                if(GET_TYPE(v)==VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v)==OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance)==VAL_NONE)
                    v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                PUSH(v);
            } else PUSH(value_none());
        } else if(GET_TYPE(o)==VAL_DICT) {
            PUSH(dict_get(AS_DICT(o), n));
        } else PUSH(value_none());
        pc++; DISPATCH();
    }
    l_OP_SET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = POP(); Value v = POP();
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) instance_set(AS_INSTANCE(o), n, v);
        else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
        pc++; DISPATCH();
    }
    l_OP_SPAWN: {
        int ac = pc->arg; Value callee = POP(); ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac; td->args = malloc(sizeof(Value)*ac);
        for(int i=0; i<ac; i++) td->args[ac-1-i] = POP();
        pthread_t t; pthread_create(&t, NULL, thread_wrapper, td); pthread_detach(t); pc++; DISPATCH();
    }
    l_OP_CALL_VALUE: {
        int ac = pc->arg; Value callee = POP();
        if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(callee);
            if(ot==OBJ_CLASS) { PUSH(value_instance(instance_new(AS_CLASS(callee)))); }
            else if(ot==OBJ_BOUND_METHOD) {
                BoundMethod *bm = AS_BOUND_METHOD(callee); Value s = var_get("self"); var_set("self", bm->instance);
                if(bm->method && bm->method->body) {
                    FuncEntry *mfe = bm->method;
                    int total = mfe->param_count + mfe->local_count;
                    Value *saved = total > 0 ? malloc(sizeof(Value) * total) : NULL;
                    for(int i=0; i<mfe->param_count; i++) saved[i] = var_get(mfe->params[i]);
                    for(int i=0; i<mfe->local_count; i++) saved[mfe->param_count+i] = var_get(mfe->local_names[i]);
                    for(int i=0; i<mfe->param_count && i<ac; i++) var_set(mfe->params[i], vm_stack[sp-ac+i]);
                    sp -= ac; Value res = run_bytecode(mfe->body, tsp);
                    for(int i=0; i<mfe->param_count; i++) var_set(mfe->params[i], saved[i]);
                    for(int i=0; i<mfe->local_count; i++) var_set(mfe->local_names[i], saved[mfe->param_count+i]);
                    if(saved) free(saved); if(has_exception) { PUSH(pending_exception); var_set("self", s); goto l_OP_THROW; } PUSH(res);
                } else { sp -= ac; PUSH(value_none()); }
                var_set("self", s);
            }
            else if(ot==OBJ_NATIVE_FUNC) { NativeFunc *nf = AS_NATIVE_FUNC(callee); Value res = nf->func(ac, &vm_stack[sp-ac]); sp-=ac; PUSH(res); }
            else PUSH(value_none());
        } else PUSH(value_none());
        pc++; DISPATCH();
    }
    l_OP_LIST_LITERAL: { int n = pc->arg; List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, vm_stack[sp-n+i]); sp-=n; PUSH(value_list(l)); pc++; DISPATCH(); }
    l_OP_INDEX: { Value i = POP(), o = POP(); if(GET_TYPE(o)==VAL_LIST && GET_TYPE(i)==VAL_INT) { int idx = AS_INT(i); if(idx>=0 && idx<AS_LIST(o)->size) PUSH(AS_LIST(o)->items[idx]); else PUSH(value_none()); } else PUSH(value_none()); pc++; DISPATCH(); }
    l_OP_STORE_INDEX: { Value i = POP(), o = POP(), v = POP(); if(GET_TYPE(o)==VAL_LIST && GET_TYPE(i)==VAL_INT) { int idx = AS_INT(i); if(idx>=0 && idx<AS_LIST(o)->size) AS_LIST(o)->items[idx] = v; } pc++; DISPATCH(); }
    l_OP_RANGE: { int n = AS_INT(POP()); List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, value_int(i)); PUSH(value_list(l)); pc++; DISPATCH(); }
    l_OP_STOP: return value_none();
    l_OP_SKIP: pc++; DISPATCH();
    l_OP_METHOD_DEF: {
        const char *n = (const char*)pc->ptr; FuncEntry *fe = func_get(n);
        if(vm_last_class) dict_set(vm_last_class->methods, n, bound_method_new(value_none(), fe));
        else if(vm_last_trait) dict_set(vm_last_trait->methods, n, bound_method_new(value_none(), fe));
        pc++; DISPATCH();
    }
    l_OP_LEN: { Value v = POP(); if(GET_TYPE(v)==VAL_STRING) PUSH(value_int(strlen(AS_STRING(v)))); else if(GET_TYPE(v)==VAL_LIST) PUSH(value_int(AS_LIST(v)->size)); else PUSH(value_int(0)); pc++; DISPATCH(); }
"""

implemented = set(["OP_CONST_INT", "OP_CONST_FLOAT", "OP_CONST_STRING", "OP_CONST_BOOL", "OP_LOAD_VAR", "OP_STORE_VAR", "OP_ADD", "OP_SUB", "OP_MUL", "OP_DIV", "OP_MOD", "OP_POW", "OP_NEG", "OP_LT", "OP_GT", "OP_LTE", "OP_GTE", "OP_EQ", "OP_NEQ", "OP_AND", "OP_OR", "OP_NOT", "OP_JUMP", "OP_JUMP_IF_FALSE", "OP_JUMP_IF_TRUE", "OP_CALL", "OP_RETURN", "OP_POP", "OP_SHOW", "OP_BUILTIN_SHOW", "OP_STORE_LET", "OP_TRY_START", "OP_TRY_END", "OP_THROW", "OP_CLASS_DEF", "OP_METHOD_DEF", "OP_TRAIT_DEF", "OP_USE_TRAIT", "OP_GET_PROPERTY", "OP_SET_PROPERTY", "OP_CALL_VALUE", "OP_LIST_LITERAL", "OP_INDEX", "OP_STORE_INDEX", "OP_RANGE", "OP_STOP", "OP_SKIP", "OP_SPAWN", "OP_LEN", "OP_FUNC_DEF", "OP_STRING_CONCAT"])

for op in all_ops:
    if op not in implemented:
        new_run_bytecode += f"    l_{op}: pc++; DISPATCH();\n"

new_run_bytecode += "    return value_none();\n}\n"

start_marker = "static Value run_bytecode(Bytecode *bc, int base_tsp) {"
end_marker = "void print_value(Value v) {"
start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx != -1 and end_idx != -1:
    content = content[:start_idx] + new_run_bytecode + content[end_idx:]
    with open("src/glowlang.y", "w") as f:
        f.write(content)
else:
    print("Could not find markers")
