import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Correct the Compiler for exceptions and control flow
comp_logic = """
        case NODE_IF: {
            compile(n->left); int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            if (n->third) compile(n->third);
            compiling_bc->code[j].arg = compiling_bc->count;
            break;
        }
        case NODE_WHILE: {
            int start = compiling_bc->count; compile(n->left);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            break;
        }
        case NODE_TRY_CATCH: {
            int ts = bc_emit(compiling_bc, OP_TRY_START, 0, 0);
            compile(n->left); bc_emit(compiling_bc, OP_TRY_END, 0, 0);
            int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[ts].arg = compiling_bc->count;
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            compile(n->right);
            compiling_bc->code[j].arg = compiling_bc->count;
            break;
        }
        case NODE_THROW: compile(n->left); bc_emit(compiling_bc, OP_THROW, 0, 0); break;
        case NODE_SPAWN: {
            int ac = n->call_arg_count; for (int i=0; i<ac; i++) compile(n->call_args[i]);
            compile(n->left); bc_emit(compiling_bc, OP_SPAWN, ac, 0); break;
        }
"""
text = re.sub(r"case NODE_CLASS: \{.*?default: break;", comp_logic + "        case NODE_CLASS: { Class *pc = last_compiling_class; last_compiling_class = (Class*)1; bc_emit(compiling_bc, OP_CLASS_DEF, 0, (intptr_t)n->name); compile(n->left); last_compiling_class = pc; break; }\n        default: break;", text, flags=re.DOTALL)

# 2. Rewrite run_bytecode to be perfectly aligned with labels and handle exceptions
vm_impl = """
static Value run_bytecode(Bytecode *bc, int base_offset) {
    static void* dt[] = {
        &&l_int, &&l_float, &&l_str, &&l_bool, &&l_load, &&l_store, &&l_add, &&l_sub, &&l_mul, &&l_div, &&l_mod, &&l_pow, &&l_neg,
        &&l_lt, &&l_gt, &&l_lte, &&l_gte, &&l_eq, &&l_neq, &&l_and, &&l_or, &&l_not, &&l_concat, &&l_jump, &&l_jf, &&l_jt,
        &&l_len, &&l_range, &&l_type, &&l_append, &&l_input, &&l_show, &&l_write, &&l_list, &&l_dict, &&l_idx, &&l_slice, &&l_sidx,
        &&l_fdef, &&l_call, &&l_ret, &&l_for, &&l_forp, &&l_stop, &&l_skip, &&l_pop, &&l_ps, &&l_pos, &&l_sl, &&l_ll, &&l_sloc,
        &&l_dk, &&l_dv, &&l_dh, &&l_dr, &&l_dm, &&l_abs, &&l_min, &&l_max, &&l_rnd, &&l_flr, &&l_ceil, &&l_sqrt, &&l_upper,
        &&l_lower, &&l_trim, &&l_find, &&l_repl, &&l_sw, &&l_ew, &&l_split, &&l_push, &&l_lpop, &&l_ins, &&l_lrem, &&l_cont,
        &&l_sort, &&l_rev, &&l_fr, &&l_fw, &&l_fa, &&l_fstr, &&l_lcomp, &&l_thr, &&l_ts, &&l_te, &&l_lam, &&l_cv, &&l_cdef,
        &&l_mdef, &&l_tdef, &&l_ut, &&l_gp, &&l_sp, &&l_spawn
    };
    int ip = 0;
    #define DISPATCH() do { if(ip >= bc->count) return value_none(); goto *dt[bc->code[ip].op]; } while(0)
    DISPATCH();

    l_int: PUSH(value_int(bc->code[ip].arg)); ip++; DISPATCH();
    l_float: PUSH(*(Value*)bc->code[ip].ptr); ip++; DISPATCH();
    l_str: PUSH(value_string_copy((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
    l_bool: PUSH(value_bool(bc->code[ip].arg)); ip++; DISPATCH();
    l_load: PUSH(var_get((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
    l_store: var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();
    l_add: {
        Value b = POP(), a = POP();
        if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
            char buf1[64], buf2[64]; const char *s1, *s2;
            if (GET_TYPE(a) == VAL_STRING) s1 = AS_STRING(a); else if(GET_TYPE(a)==VAL_INT) sprintf(buf1, "%ld", (long)AS_INT(a)), s1 = buf1; else s1="none";
            if (GET_TYPE(b) == VAL_STRING) s2 = AS_STRING(b); else if(GET_TYPE(b)==VAL_INT) sprintf(buf2, "%ld", (long)AS_INT(b)), s2 = buf2; else s2="none";
            char *res = malloc(strlen(s1) + strlen(s2) + 1); strcpy(res, s1); strcat(res, s2);
            PUSH(value_string_copy(res)); free(res);
        } else if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) + AS_INT(b)));
        else PUSH(value_float(((GET_TYPE(a)==VAL_INT)?(double)AS_INT(a):AS_FLOAT(a)) + ((GET_TYPE(b)==VAL_INT)?(double)AS_INT(b):AS_FLOAT(b))));
        ip++; DISPATCH();
    }
    l_sub: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) - AS_INT(b))); ip++; DISPATCH(); }
    l_mul: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) * AS_INT(b))); ip++; DISPATCH(); }
    l_div: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) / AS_INT(b))); ip++; DISPATCH(); }
    l_mod: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) % AS_INT(b))); ip++; DISPATCH(); }
    l_pow: { Value b = POP(), a = POP(); PUSH(value_float(pow(AS_FLOAT(a), AS_FLOAT(b)))); ip++; DISPATCH(); }
    l_neg: { Value a = POP(); PUSH(value_int(-AS_INT(a))); ip++; DISPATCH(); }
    l_lt: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) < AS_INT(b))); ip++; DISPATCH(); }
    l_gt: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) > AS_INT(b))); ip++; DISPATCH(); }
    l_lte: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) <= AS_INT(b))); ip++; DISPATCH(); }
    l_gte: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) >= AS_INT(b))); ip++; DISPATCH(); }
    l_eq: { Value b = POP(), a = POP(); PUSH(value_bool(a == b)); ip++; DISPATCH(); }
    l_neq: { Value b = POP(), a = POP(); PUSH(value_bool(a != b)); ip++; DISPATCH(); }
    l_and: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) && value_truthy(b))); ip++; DISPATCH(); }
    l_or: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) || value_truthy(b))); ip++; DISPATCH(); }
    l_not: { Value a = POP(); PUSH(value_bool(!value_truthy(a))); ip++; DISPATCH(); }
    l_concat: ip++; DISPATCH();
    l_jump: ip = bc->code[ip].arg; DISPATCH();
    l_jf: { Value v = POP(); if (!value_truthy(v)) ip = bc->code[ip].arg; else ip++; DISPATCH(); }
    l_jt: { Value v = POP(); if (value_truthy(v)) ip = bc->code[ip].arg; else ip++; DISPATCH(); }
    l_show: { int n = bc->code[ip].arg; for (int i=0; i<n; i++) { print_value(vm_stack[sp-n+i]); if(i<n-1) printf(" "); } printf("\\n"); fflush(stdout); sp-=n; ip++; DISPATCH(); }
    l_cdef: { const char *n = (const char*)bc->code[ip].ptr; Value c = value_class(class_new(n, NULL)); var_set(n, c); vm_last_class = AS_CLASS(c); ip++; DISPATCH(); }
    l_mdef: { const char *n = (const char*)bc->code[ip].ptr; FuncEntry *fe = func_get(n); if(vm_last_class) dict_set(vm_last_class->methods, n, bound_method_new(value_none(), fe)); ip++; DISPATCH(); }
    l_gp: {
        const char *n = (const char*)bc->code[ip].ptr; Value o = POP();
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Value v = instance_get(AS_INSTANCE(o), n);
                if(GET_TYPE(v)==VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v)==OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance)==VAL_NONE)
                    v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                PUSH(v); ip++; DISPATCH();
            }
        }
        PUSH(value_none()); ip++; DISPATCH();
    }
    l_sp: {
        const char *n = (const char*)bc->code[ip].ptr; Value o = POP(); Value v = vm_stack[sp-1];
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) instance_set(AS_INSTANCE(o), n, v);
        ip++; DISPATCH();
    }
    l_cv: {
        int ac = bc->code[ip].arg; Value callee = POP();
        if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(callee);
            if(ot==OBJ_CLASS) { PUSH(value_instance(instance_new(AS_CLASS(callee)))); }
            else if(ot==OBJ_BOUND_METHOD) { 
                BoundMethod *bm = AS_BOUND_METHOD(callee); Value s = var_get("self"); var_set("self", bm->instance);
                if(bm->method && bm->method->body) {
                    Value *saved = (bm->method->param_count > 0 && bm->method->params) ? malloc(sizeof(Value)*bm->method->param_count) : NULL;
                    for(int i=0; i<bm->method->param_count && i<ac; i++) { saved[i] = var_get(bm->method->params[i]); if(sp-ac+i>=0) var_set(bm->method->params[i], vm_stack[sp-ac+i]); }
                    sp -= ac; Value res = run_bytecode(bm->method->body, 0);
                    for(int i=0; i<bm->method->param_count && i<ac; i++) var_set(bm->method->params[i], saved[i]);
                    if(saved) free(saved);
                    if(has_exception) { PUSH(pending_exception); var_set("self", s); goto l_thr; }
                    PUSH(res);
                } else { sp -= ac; PUSH(value_none()); }
                var_set("self", s); 
            }
            else if(ot==OBJ_NATIVE_FUNC) { NativeFunc *nf = AS_NATIVE_FUNC(callee); Value *args = &vm_stack[sp-ac]; Value res = nf->func(ac, args); sp-=ac; PUSH(res); }
        }
        ip++; DISPATCH();
    }
    l_call: {
        int ac = bc->code[ip].arg; const char *n = (const char*)bc->code[ip].ptr; FuncEntry *fe = func_get(n);
        if (fe && fe->body) {
            Value *saved = (fe->param_count > 0 && fe->params) ? malloc(sizeof(Value)*fe->param_count) : NULL;
            for(int i=0; i<fe->param_count && i<ac; i++) { saved[i] = var_get(fe->params[i]); if(sp-ac+i>=0) var_set(fe->params[i], vm_stack[sp-ac+i]); }
            sp -= ac; Value res = run_bytecode(fe->body, 0);
            for(int i=0; i<fe->param_count && i<ac; i++) var_set(fe->params[i], saved[i]);
            if(saved) free(saved);
            if(has_exception) { PUSH(pending_exception); goto l_thr; }
            PUSH(res);
        } else { sp -= ac; PUSH(value_none()); }
        ip++; DISPATCH();
    }
    l_list: { int n = bc->code[ip].arg; List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, vm_stack[sp-n+i]); sp-=n; PUSH(value_list(l)); ip++; DISPATCH(); }
    l_idx: { Value i = POP(), o = POP(); if(GET_TYPE(o)==VAL_LIST && GET_TYPE(i)==VAL_INT) { int idx = AS_INT(i); if(idx>=0 && idx<AS_LIST(o)->size) PUSH(AS_LIST(o)->items[idx]); else PUSH(value_none()); } else PUSH(value_none()); ip++; DISPATCH(); }
    l_ret: return sp > 0 ? POP() : value_none();
    l_pop: POP(); ip++; DISPATCH();
    l_ts: try_stack[tsp++] = (TryHandler){ bc->code[ip].arg, sp }; ip++; DISPATCH();
    l_te: tsp--; ip++; DISPATCH();
    l_thr: {
        Value exc = POP();
        if (tsp > 0) { TryHandler h = try_stack[--tsp]; sp = h.sp; PUSH(exc); has_exception = 0; ip = h.handler_ip; DISPATCH(); }
        pending_exception = exc; has_exception = 1; return value_none();
    }
    l_spawn: {
        int ac = bc->code[ip].arg; Value callee = POP(); ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac; td->args = malloc(sizeof(Value)*ac);
        for(int i=0; i<ac; i++) td->args[ac-1-i] = POP();
        pthread_t t; pthread_create(&t, NULL, thread_wrapper, td); pthread_detach(t); ip++; DISPATCH();
    }
    l_len: l_range: l_type: l_append: l_input: l_write: l_dict: l_slice: l_sidx: l_fdef: l_for: l_forp: l_stop: l_skip: l_ps: l_pos: l_sl: l_ll: l_sloc: l_dk: l_dv: l_dh: l_dr: l_dm: l_abs: l_min: l_max: l_rnd: l_flr: l_ceil: l_sqrt: l_upper: l_lower: l_trim: l_find: l_repl: l_sw: l_ew: l_split: l_push: l_lpop: l_ins: l_lrem: l_cont: l_sort: l_rev: l_fr: l_fw: l_fa: l_fstr: l_lcomp: l_lam: l_tdef: l_ut: ip++; DISPATCH();
}
"""
text = re.sub(r"static Value run_bytecode.*?\}", vm_impl, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
