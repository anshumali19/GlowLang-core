static void print_value(Value v) {
    if (GET_TYPE(v) == VAL_INT)    printf("%d", AS_INT(v));
    else if (GET_TYPE(v) == VAL_FLOAT)  printf("%g", AS_FLOAT(v));
    else if (GET_TYPE(v) == VAL_BOOL)   printf("%s", AS_BOOL(v) ? "true" : "false");
    else if (GET_TYPE(v) == VAL_STRING) printf("%s", AS_STRING(v));
    else if (GET_TYPE(v) == VAL_LIST) {
        printf("[");
        for (int j = 0; j < AS_LIST(v)->size; j++) {
            print_value(AS_LIST(v)->items[j]);
            if (j < AS_LIST(v)->size - 1) printf(", ");
        }
        printf("]");
    }
    else if (GET_TYPE(v) == VAL_DICT) {
        printf("{");
        for (int j = 0; j < AS_DICT(v)->size; j++) {
            printf("\"%s\": ", AS_DICT(v)->keys[j]);
            print_value(AS_DICT(v)->values[j]);
            if (j < AS_DICT(v)->size - 1) printf(", ");
        }
        printf("}");
    }
}

static Value run_bytecode(Bytecode *bc, int base_offset) {
    static void* dispatch_table[] = {
        &&label_OP_CONST_INT, &&label_OP_CONST_FLOAT, &&label_OP_CONST_STRING, &&label_OP_CONST_BOOL,
        &&label_OP_LOAD_VAR, &&label_OP_STORE_VAR,
        &&label_OP_ADD, &&label_OP_SUB, &&label_OP_MUL, &&label_OP_DIV, &&label_OP_MOD, &&label_OP_POW, &&label_OP_NEG,
        &&label_OP_LT, &&label_OP_GT, &&label_OP_LTE, &&label_OP_GTE, &&label_OP_EQ, &&label_OP_NEQ,
        &&label_OP_AND, &&label_OP_OR, &&label_OP_NOT,
        &&label_OP_STRING_CONCAT,
        &&label_OP_JUMP, &&label_OP_JUMP_IF_FALSE, &&label_OP_JUMP_IF_TRUE,
        &&label_OP_CALL_BUILTIN_LEN, &&label_OP_CALL_BUILTIN_RANGE, &&label_OP_CALL_BUILTIN_TYPE,
        &&label_OP_BUILTIN_APPEND, &&label_OP_BUILTIN_INPUT, &&label_OP_BUILTIN_SHOW, &&label_OP_BUILTIN_WRITE,
        &&label_OP_LIST_LITERAL, &&label_OP_DICT_LITERAL,
        &&label_OP_INDEX, &&label_OP_SLICE, &&label_OP_STORE_INDEX,
        &&label_OP_FUNC_DEF, &&label_OP_CALL, &&label_OP_RETURN,
        &&label_OP_FOR_EACH, &&label_OP_FOR_EACH_PAIR,
        &&label_OP_STOP, &&label_OP_SKIP,
        &&label_OP_POP,
        &&label_OP_PUSH_SCOPE, &&label_OP_POP_SCOPE, &&label_OP_STORE_LET,
        &&label_OP_LOAD_LOCAL, &&label_OP_STORE_LOCAL,
        &&label_OP_DICT_KEYS, &&label_OP_DICT_VALUES, &&label_OP_DICT_HAS, &&label_OP_DICT_REMOVE, &&label_OP_DICT_MERGE,
        &&label_OP_MATH_ABS, &&label_OP_MATH_MIN, &&label_OP_MATH_MAX, &&label_OP_MATH_ROUND, &&label_OP_MATH_FLOOR, &&label_OP_MATH_CEIL, &&label_OP_MATH_SQRT,
        &&label_OP_STR_UPPER, &&label_OP_STR_LOWER, &&label_OP_STR_TRIM, &&label_OP_STR_FIND, &&label_OP_STR_REPLACE, &&label_OP_STR_STARTSWITH, &&label_OP_STR_ENDSWITH, &&label_OP_STR_SPLIT,
        &&label_OP_LIST_PUSH, &&label_OP_LIST_POP, &&label_OP_LIST_INSERT, &&label_OP_LIST_REMOVE, &&label_OP_LIST_CONTAINS, &&label_OP_LIST_SORT, &&label_OP_LIST_REVERSE,
        &&label_OP_FILE_READ, &&label_OP_FILE_WRITE, &&label_OP_FILE_APPEND,
        &&label_OP_FSTRING,
        &&label_OP_LIST_COMP,
        &&label_OP_THROW, &&label_OP_TRY_START, &&label_OP_TRY_END,
        &&label_OP_LAMBDA,
        &&label_OP_CALL_VALUE
    };

    int ip = 0;
    int base_sp = sp;
    vm_loop_stop = 0; vm_loop_skip = 0; vm_has_return = 0;

    #define DISPATCH() \
        do { \
            if (ip >= bc->count) goto label_EXIT_VM; \
            if (vm_exception_active) goto label_HANDLE_EXCEPTION; \
            goto *dispatch_table[bc->code[ip].op]; \
        } while (0)

    DISPATCH();

label_HANDLE_EXCEPTION:
    if (hsp > 0) {
        if (handler_stack[hsp-1].csp == csp) {
            TryHandler h = handler_stack[--hsp];
            vm_exception_active = 0;
            ip = h.catch_ip; sp = h.sp;
            var_restore_shadow(h.shadow_list);
            PUSH(vm_exception_value);
            DISPATCH();
        } else {
            if (csp > 0) {
                csp--; CallFrame *cf = &call_stack[csp];
                if ((cf->pending_fn && cf->pending_fn->uses_let) || cf->pending_closure) var_restore_shadow(cf->old_shadow_list);
                if (cf->param_count > 0) var_set(cf->p0, cf->v0);
                if (cf->param_count > 1) var_set(cf->p1, cf->v1);
                if (cf->local_values && cf->local_count > 16) free(cf->local_values);
                bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
                vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
                DISPATCH();
            } else {
                printf("Unhandled exception: ");
                if (GET_TYPE(vm_exception_value) == VAL_STRING) printf("%s\n", AS_STRING(vm_exception_value));
                else printf("type %s\n", type_name(GET_TYPE(vm_exception_value)));
                exit(1);
            }
        }
    } else {
        printf("Unhandled exception at top level\n");
        exit(1);
    }

label_OP_CONST_INT:
    PUSH(value_int(bc->code[ip].arg)); ip++; DISPATCH();

label_OP_CONST_FLOAT:
    PUSH(value_float(*(double*)bc->code[ip].ptr)); ip++; DISPATCH();

label_OP_CONST_STRING:
    PUSH(value_string((const char*)bc->code[ip].ptr)); ip++; DISPATCH();

label_OP_CONST_BOOL:
    PUSH(value_bool(bc->code[ip].arg)); ip++; DISPATCH();

label_OP_LOAD_VAR:
    PUSH(var_get((const char*)bc->code[ip].ptr)); ip++; DISPATCH();

label_OP_STORE_VAR:
    var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();

label_OP_LOAD_LOCAL: {
    int idx = bc->code[ip].arg;
    if (csp > 0) {
        CallFrame *cf = &call_stack[csp-1];
        if (cf->local_values && idx < cf->local_count) PUSH(cf->local_values[idx]);
        else PUSH(value_int(0));
    } else PUSH(var_get((const char*)bc->code[ip].ptr));
    ip++; DISPATCH();
}

label_OP_STORE_LOCAL: {
    int idx = bc->code[ip].arg; Value v = POP();
    if (csp > 0) {
        CallFrame *cf = &call_stack[csp-1];
        if (cf->local_values && idx < cf->local_count) cf->local_values[idx] = v;
    } else var_set((const char*)bc->code[ip].ptr, v);
    ip++; DISPATCH();
}

label_OP_ADD: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) + AS_INT(b)));
    else if (GET_TYPE(a) == VAL_FLOAT && GET_TYPE(b) == VAL_FLOAT) PUSH(value_float(AS_FLOAT(a) + AS_FLOAT(b)));
    else if (GET_TYPE(a) == VAL_STRING && GET_TYPE(b) == VAL_STRING) {
        char *s = malloc(strlen(AS_STRING(a)) + strlen(AS_STRING(b)) + 1);
        strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b)); PUSH(value_string(s));
    } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
        List *l1 = AS_LIST(a), *l2 = AS_LIST(b); List *res = list_new(l1->size + l2->size);
        for (int i = 0; i < l1->size; i++) { res->items[i] = l1->items[i]; if (GET_TYPE(res->items[i]) == VAL_LIST) list_incref(AS_LIST(res->items[i])); if (GET_TYPE(res->items[i]) == VAL_DICT) dict_incref(AS_DICT(res->items[i])); }
        for (int i = 0; i < l2->size; i++) { res->items[l1->size + i] = l2->items[i]; if (GET_TYPE(res->items[l1->size + i]) == VAL_LIST) list_incref(AS_LIST(res->items[l1->size + i])); if (GET_TYPE(res->items[l1->size + i]) == VAL_DICT) dict_incref(AS_DICT(res->items[l1->size + i])); }
        res->size = l1->size + l2->size; PUSH(value_list(res));
    } else {
        double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
        PUSH(value_float(da + db));
    }
    ip++; DISPATCH();
}

label_OP_SUB: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) - AS_INT(b)));
    else PUSH(value_float(val_to_int_or_float(a) - val_to_int_or_float(b)));
    ip++; DISPATCH();
}

label_OP_MUL: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) * AS_INT(b)));
    else PUSH(value_float(val_to_int_or_float(a) * val_to_int_or_float(b)));
    ip++; DISPATCH();
}

label_OP_DIV: {
    Value b = POP(), a = POP();
    double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
    if (db == 0.0) { printf("Error: Division by zero\n"); PUSH(value_float(0)); }
    else PUSH(value_float(da / db));
    ip++; DISPATCH();
}

label_OP_MOD: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
        if (AS_INT(b) == 0) { printf("Error: Modulo by zero\n"); PUSH(value_int(0)); }
        else PUSH(value_int(AS_INT(a) % AS_INT(b)));
    } else {
        double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
        if (db == 0.0) { printf("Error: Modulo by zero\n"); PUSH(value_float(0)); }
        else PUSH(value_float(fmod(da, db)));
    }
    ip++; DISPATCH();
}

label_OP_POW: {
    Value b = POP(), a = POP();
    PUSH(value_float(pow(val_to_int_or_float(a), val_to_int_or_float(b))));
    ip++; DISPATCH();
}

label_OP_NEG: {
    Value a = POP();
    if (GET_TYPE(a) == VAL_INT) PUSH(value_int(-AS_INT(a)));
    else PUSH(value_float(-val_to_int_or_float(a)));
    ip++; DISPATCH();
}

label_OP_LT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) < val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_GT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) > val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_LTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) <= val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_GTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) >= val_to_int_or_float(b))); ip++; DISPATCH(); }

label_OP_EQ: {
    Value b = POP(), a = POP(); int res = 0;
    if (GET_TYPE(a) == GET_TYPE(b)) {
        switch (GET_TYPE(a)) {
            case VAL_INT: res = AS_INT(a) == AS_INT(b); break;
            case VAL_FLOAT: res = AS_FLOAT(a) == AS_FLOAT(b); break;
            case VAL_BOOL: res = AS_BOOL(a) == AS_BOOL(b); break;
            case VAL_STRING: res = strcmp(AS_STRING(a), AS_STRING(b)) == 0; break;
            default: res = (a == b); break;
        }
    } else res = (val_to_int_or_float(a) == val_to_int_or_float(b));
    PUSH(value_bool(res)); ip++; DISPATCH();
}

label_OP_NEQ: {
    Value b = POP(), a = POP(); PUSH(value_bool(!val_eq(a, b))); ip++; DISPATCH();
}

label_OP_AND: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) && val_to_bool(b))); ip++; DISPATCH(); }
label_OP_OR: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) || val_to_bool(b))); ip++; DISPATCH(); }
label_OP_NOT: PUSH(value_bool(!val_to_bool(POP()))); ip++; DISPATCH();

label_OP_STRING_CONCAT: {
    Value b = POP(), a = POP();
    char *s = malloc(strlen(AS_STRING(a)) + strlen(AS_STRING(b)) + 1);
    strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b)); PUSH(value_string(s));
    ip++; DISPATCH();
}

label_OP_JUMP:
    ip = bc->code[ip].arg - base_offset; DISPATCH();

label_OP_JUMP_IF_FALSE:
    if (!val_to_bool(vm_stack[sp-1])) ip = bc->code[ip].arg - base_offset; else ip++;
    DISPATCH();

label_OP_JUMP_IF_TRUE:
    if (val_to_bool(vm_stack[sp-1])) ip = bc->code[ip].arg - base_offset; else ip++;
    DISPATCH();

label_OP_CALL_BUILTIN_LEN: {
    Value v = POP(); int len = 0;
    if (GET_TYPE(v) == VAL_LIST) len = AS_LIST(v)->size;
    else if (GET_TYPE(v) == VAL_STRING) len = (int)strlen(AS_STRING(v));
    else if (GET_TYPE(v) == VAL_DICT) len = AS_DICT(v)->size;
    PUSH(value_int(len)); ip++; DISPATCH();
}

label_OP_CALL_BUILTIN_RANGE: {
    int n = val_to_int(POP()); List *l = list_new(n < 0 ? 0 : n);
    for (int i = 0; i < n; i++) l->items[i] = value_int(i);
    PUSH(value_list(l)); ip++; DISPATCH();
}

label_OP_CALL_BUILTIN_TYPE:
    PUSH(value_string(type_name(GET_TYPE(POP())))); ip++; DISPATCH();

label_OP_BUILTIN_APPEND: {
    Value item = POP(), lv = POP();
    if (GET_TYPE(lv) == VAL_LIST) list_push(AS_LIST(lv), item);
    PUSH(lv); ip++; DISPATCH();
}

label_OP_BUILTIN_INPUT: {
    if (bc->code[ip].arg == 1) { Value p = POP(); if (GET_TYPE(p) == VAL_STRING) { printf("%s", AS_STRING(p)); fflush(stdout); } }
    char buf[4096]; if (fgets(buf, sizeof(buf), stdin)) {
        int len = (int)strlen(buf); while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        PUSH(value_string(strdup(buf)));
    } else PUSH(value_string(""));
    ip++; DISPATCH();
}

label_OP_BUILTIN_SHOW: {
    int n = bc->code[ip].arg; if (n == 0) printf("\n");
    else { for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); } printf("\n"); }
    ip++; DISPATCH();
}

label_OP_BUILTIN_WRITE: {
    int n = bc->code[ip].arg; for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); }
    fflush(stdout); ip++; DISPATCH();
}

label_OP_LIST_LITERAL: {
    int n = bc->code[ip].arg; List *l = list_new(n);
    for (int i = n-1; i >= 0; i--) l->items[i] = POP();
    PUSH(value_list(l)); ip++; DISPATCH();
}

label_OP_DICT_LITERAL: {
    int n = bc->code[ip].arg; Dict *d = dict_new(n);
    for (int i = 0; i < n; i++) { Value v = POP(), k = POP(); dict_set(d, AS_STRING(k), v); }
    PUSH(value_dict(d)); ip++; DISPATCH();
}

label_OP_INDEX: {
    Value idx = POP(), coll = POP();
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
        if (i < 0 || i >= AS_LIST(coll)->size) PUSH(value_int(0));
        else PUSH(AS_LIST(coll)->items[i]);
    } else if (GET_TYPE(coll) == VAL_STRING) {
        int i = val_to_int(idx); int slen = (int)strlen(AS_STRING(coll)); if (i < 0) i += slen;
        if (i < 0 || i >= slen) PUSH(value_int(0));
        else { char b[2] = {AS_STRING(coll)[i], 0}; PUSH(value_string(strdup(b))); }
    } else if (GET_TYPE(coll) == VAL_DICT) {
        int found = 0; for (int i = 0; i < AS_DICT(coll)->size; i++) {
            if (strcmp(AS_DICT(coll)->keys[i], AS_STRING(idx)) == 0) { PUSH(AS_DICT(coll)->values[i]); found = 1; break; }
        }
        if (!found) PUSH(value_int(0));
    } else PUSH(value_int(0));
    ip++; DISPATCH();
}

label_OP_SLICE: {
    Value eV = POP(), sV = POP(), src = POP();
    int slen = GET_TYPE(src) == VAL_STRING ? (int)strlen(AS_STRING(src)) : (GET_TYPE(src) == VAL_LIST ? AS_LIST(src)->size : 0);
    int s = (GET_TYPE(sV) == VAL_INT && AS_INT(sV) == -1) ? 0 : val_to_int(sV);
    int e = (GET_TYPE(eV) == VAL_INT && AS_INT(eV) == -1) ? slen : val_to_int(eV);
    if (s < 0) s += slen; if (e < 0) e += slen; if (s < 0) s = 0; if (e > slen) e = slen; if (s > e) s = e;
    if (GET_TYPE(src) == VAL_STRING) { char *sub = malloc(e - s + 1); strncpy(sub, AS_STRING(src) + s, e - s); sub[e-s] = 0; PUSH(value_string(sub)); }
    else if (GET_TYPE(src) == VAL_LIST) {
        List *rl = list_new(e - s); for (int i = 0; i < e - s; i++) { rl->items[i] = AS_LIST(src)->items[s + i]; if (GET_TYPE(rl->items[i]) == VAL_LIST) list_incref(AS_LIST(rl->items[i])); if (GET_TYPE(rl->items[i]) == VAL_DICT) dict_incref(AS_DICT(rl->items[i])); }
        PUSH(value_list(rl));
    } else PUSH(value_int(0));
    ip++; DISPATCH();
}

label_OP_STORE_INDEX: {
    Value v = POP(), idx = POP(), coll = POP();
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
        if (i >= 0 && i < AS_LIST(coll)->size) AS_LIST(coll)->items[i] = v;
    } else if (GET_TYPE(coll) == VAL_DICT && GET_TYPE(idx) == VAL_STRING) dict_set(AS_DICT(coll), AS_STRING(idx), v);
    ip++; DISPATCH();
}

label_OP_FUNC_DEF: ip++; DISPATCH();

label_OP_CALL: {
    FuncEntry *fn = func_get((const char*)bc->code[ip].ptr);
    if (fn) {
        if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = fn; cf->pending_closure = NULL; cf->local_count = fn->local_count;
        if (cf->local_count > 0) {
            if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) cf->local_values[i] = value_none(); }
            else cf->local_values = calloc(cf->local_count, sizeof(Value));
        } else cf->local_values = NULL;
        if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
        for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
        
        Value rv = run_bytecode(fn->body, 0);

        /* Restore caller state */
        csp--;
        cf = &call_stack[csp];
        if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        if (cf->local_values && cf->local_count > 16) free(cf->local_values);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else {
        Value v = var_get((const char*)bc->code[ip].ptr);
        if (GET_TYPE(v) == VAL_CLOSURE) {
            Closure *c = AS_CLOSURE(v); if (csp >= CALL_STACK_SIZE-1) exit(1);
            CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
            cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
            cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
            for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
            Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
            for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
            cf->param_count = c->param_count < 2 ? c->param_count : 2;
            if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
            if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
            for (int i=0; i<c->param_count; i++) var_set_let(c->params[i], ab[i]);
            if (c->param_count > 8) free(ab);
            
            Value rv = run_bytecode(c->body, 0);
            
            csp--;
            cf = &call_stack[csp];
            var_restore_shadow(cf->old_shadow_list);
            if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
            bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
            vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
            PUSH(rv); DISPATCH();
        }
        ip++; DISPATCH();
    }
}

label_OP_RETURN: {
    total_returns++; Value rv = (sp > base_sp) ? POP() : value_none(); 
    return rv;
}

label_OP_FOR_EACH: {
    Value iter = POP(); const char *vn = (const char*)bc->code[ip].ptr;
    int bs = ip + 1; int be = bc->code[ip].arg;
    if (GET_TYPE(iter) == VAL_LIST) {
        for (int i=0; i<AS_LIST(iter)->size; i++) {
            vm_loop_skip = 0; 
            int found_local = -1;
            if (csp > 0) {
                CallFrame *outer = &call_stack[csp-1];
                if (outer->pending_fn) {
                    for (int j=0; j<outer->pending_fn->local_count; j++) {
                        if (strcmp(outer->pending_fn->local_names[j], vn) == 0) { found_local = j; break; }
                    }
                }
            }
            if (found_local >= 0) call_stack[csp-1].local_values[found_local] = AS_LIST(iter)->items[i];
            else var_set(vn, AS_LIST(iter)->items[i]); 

            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
        }
    } else if (GET_TYPE(iter) == VAL_STRING) {
        int len = strlen(AS_STRING(iter)); for (int i=0; i<len; i++) {
            char b[2] = {AS_STRING(iter)[i], 0}; var_set(vn, value_string(strdup(b))); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
        }
    } else if (GET_TYPE(iter) == VAL_DICT) {
        for (int i=0; i<AS_DICT(iter)->size; i++) {
            var_set(vn, value_string(AS_DICT(iter)->keys[i])); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
        }
    }
    ip = be - base_offset; DISPATCH();
}

label_OP_FOR_EACH_PAIR: {
    Value iter = POP(); const char *kn = (const char*)bc->code[ip].ptr; const char *vn = (const char*)bc->code[ip+1].ptr;
    int bs = ip + 2; int be = bc->code[ip].arg;
    if (GET_TYPE(iter) == VAL_LIST) {
        for (int i=0; i<AS_LIST(iter)->size; i++) {
            var_set(kn, value_int(i)); var_set(vn, AS_LIST(iter)->items[i]); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
        }
    } else if (GET_TYPE(iter) == VAL_DICT) {
        for (int i=0; i<AS_DICT(iter)->size; i++) {
            var_set(kn, value_string(AS_DICT(iter)->keys[i])); var_set(vn, AS_DICT(iter)->values[i]); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
        }
    }
    ip = be - base_offset; DISPATCH();
}

label_OP_STOP: vm_loop_stop = 1; ip++; DISPATCH();
label_OP_SKIP: vm_loop_skip = 1; ip++; DISPATCH();
label_OP_POP: POP(); ip++; DISPATCH();
label_OP_PUSH_SCOPE: var_push_scope(); ip++; DISPATCH();
label_OP_POP_SCOPE: var_pop_scope(); ip++; DISPATCH();
label_OP_STORE_LET: var_set_let((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();

label_OP_DICT_KEYS: {
    Value d = POP(); if (GET_TYPE(d) != VAL_DICT) { PUSH(value_int(0)); }
    else { List *l = list_new(AS_DICT(d)->size); for (int i=0; i<AS_DICT(d)->size; i++) l->items[i] = value_string(AS_DICT(d)->keys[i]); PUSH(value_list(l)); }
    ip++; DISPATCH();
}
label_OP_DICT_VALUES: {
    Value d = POP(); if (GET_TYPE(d) != VAL_DICT) { PUSH(value_int(0)); }
    else { List *l = list_new(AS_DICT(d)->size); for (int i=0; i<AS_DICT(d)->size; i++) { l->items[i] = AS_DICT(d)->values[i]; if (GET_TYPE(l->items[i]) == VAL_LIST) list_incref(AS_LIST(l->items[i])); if (GET_TYPE(l->items[i]) == VAL_DICT) dict_incref(AS_DICT(l->items[i])); } PUSH(value_list(l)); }
    ip++; DISPATCH();
}
label_OP_DICT_HAS: {
    Value k = POP(), d = POP(); int f = 0; if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { f = 1; break; } }
    PUSH(value_bool(f)); ip++; DISPATCH();
}
label_OP_DICT_REMOVE: {
    Value k = POP(), d = POP(); if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { for (int j=i; j<AS_DICT(d)->size-1; j++) { AS_DICT(d)->keys[j] = AS_DICT(d)->keys[j+1]; AS_DICT(d)->values[j] = AS_DICT(d)->values[j+1]; } AS_DICT(d)->size--; break; } }
    PUSH(d); ip++; DISPATCH();
}
label_OP_DICT_MERGE: {
    Value d2 = POP(), d1 = POP(); if (GET_TYPE(d1) == VAL_DICT && GET_TYPE(d2) == VAL_DICT) { for (int i=0; i<AS_DICT(d2)->size; i++) dict_set(AS_DICT(d1), AS_DICT(d2)->keys[i], AS_DICT(d2)->values[i]); }
    PUSH(d1); ip++; DISPATCH();
}

label_OP_MATH_ABS: {
    Value v = POP(); if (GET_TYPE(v) == VAL_INT) PUSH(value_int(abs(AS_INT(v)))); else if (GET_TYPE(v) == VAL_FLOAT) PUSH(value_float(fabs(AS_FLOAT(v)))); else PUSH(value_int(0));
    ip++; DISPATCH();
}
label_OP_MATH_MIN: {
    int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) < val_to_int_or_float(best)) best = v; }
    PUSH(best); ip++; DISPATCH();
}
label_OP_MATH_MAX: {
    int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) > val_to_int_or_float(best)) best = v; }
    PUSH(best); ip++; DISPATCH();
}
label_OP_MATH_ROUND: PUSH(value_int((int)round(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_FLOOR: PUSH(value_int((int)floor(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_CEIL: PUSH(value_int((int)ceil(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_SQRT: PUSH(value_float(sqrt(val_to_int_or_float(POP())))); ip++; DISPATCH();

label_OP_STR_UPPER: {
    Value s = POP(); if (GET_TYPE(s) == VAL_STRING) { char *u = strdup(AS_STRING(s)); for (char *p=u; *p; p++) *p = toupper(*p); PUSH(value_string(u)); } else PUSH(s);
    ip++; DISPATCH();
}
label_OP_STR_LOWER: {
    Value s = POP(); if (GET_TYPE(s) == VAL_STRING) { char *l = strdup(AS_STRING(s)); for (char *p=l; *p; p++) *p = tolower(*p); PUSH(value_string(l)); } else PUSH(s);
    ip++; DISPATCH();
}
label_OP_STR_TRIM: {
    Value s = POP(); if (GET_TYPE(s) == VAL_STRING) {
        const char *st = AS_STRING(s); while (*st && isspace(*st)) st++;
        char *ed = (char*)st + strlen(st) - 1; while (ed > st && isspace(*ed)) ed--;
        int len = ed - st + 1; if (len < 0) len = 0; char *res = malloc(len + 1); strncpy(res, st, len); res[len] = 0; PUSH(value_string(res));
    } else PUSH(s);
    ip++; DISPATCH();
}
label_OP_STR_FIND: {
    Value sub = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING) { char *p = strstr(AS_STRING(s), AS_STRING(sub)); PUSH(value_int(p ? (int)(p - AS_STRING(s)) : -1)); } else PUSH(value_int(-1));
    ip++; DISPATCH();
}
label_OP_STR_REPLACE: {
    Value r = POP(), sub = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING && GET_TYPE(r) == VAL_STRING) PUSH(value_string(str_replace(AS_STRING(s), AS_STRING(sub), AS_STRING(r)))); else PUSH(s);
    ip++; DISPATCH();
}
label_OP_STR_STARTSWITH: {
    Value p = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(p) == VAL_STRING) PUSH(value_bool(strncmp(AS_STRING(s), AS_STRING(p), strlen(AS_STRING(p))) == 0)); else PUSH(value_bool(0));
    ip++; DISPATCH();
}
label_OP_STR_ENDSWITH: {
    Value su = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(su) == VAL_STRING) { int sl = strlen(AS_STRING(s)), sul = strlen(AS_STRING(su)); PUSH(value_bool(sl >= sul && strcmp(AS_STRING(s) + sl - sul, AS_STRING(su)) == 0)); } else PUSH(value_bool(0));
    ip++; DISPATCH();
}
label_OP_STR_SPLIT: {
    Value sep = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sep) == VAL_STRING) {
        List *l = list_new(8); char *c = strdup(AS_STRING(s)), *t = strtok(c, AS_STRING(sep));
        while (t) { list_push(l, value_string(strdup(t))); t = strtok(NULL, AS_STRING(sep)); } PUSH(value_list(l)); free(c);
    } else PUSH(value_list(list_new(0)));
    ip++; DISPATCH();
}

label_OP_LIST_PUSH: { Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) list_push(AS_LIST(l), v); PUSH(l); ip++; DISPATCH(); }
label_OP_LIST_POP: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST && AS_LIST(l)->size > 0) PUSH(AS_LIST(l)->items[--AS_LIST(l)->size]); else PUSH(value_int(0)); ip++; DISPATCH(); }
label_OP_LIST_INSERT: {
    Value v = POP(), idx = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(l)->size; if (i >= 0 && i <= AS_LIST(l)->size) {
            list_push(AS_LIST(l), v); for (int j=AS_LIST(l)->size-1; j>i; j--) AS_LIST(l)->items[j] = AS_LIST(l)->items[j-1]; AS_LIST(l)->items[i] = v;
        }
    } PUSH(l); ip++; DISPATCH();
}
label_OP_LIST_REMOVE: {
    Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
        int idx = -1; for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { idx = i; break; }
        if (idx >= 0) { for (int i=idx; i<AS_LIST(l)->size-1; i++) AS_LIST(l)->items[i] = AS_LIST(l)->items[i+1]; AS_LIST(l)->size--; }
    } PUSH(l); ip++; DISPATCH();
}
label_OP_LIST_CONTAINS: {
    Value v = POP(), l = POP(); int f = 0; if (GET_TYPE(l) == VAL_LIST) for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { f = 1; break; }
    PUSH(value_bool(f)); ip++; DISPATCH();
}
label_OP_LIST_SORT: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST) qsort(AS_LIST(l)->items, AS_LIST(l)->size, sizeof(Value), val_compare); PUSH(l); ip++; DISPATCH(); }
label_OP_LIST_REVERSE: {
    Value l = POP(); if (GET_TYPE(l) == VAL_LIST) { int n = AS_LIST(l)->size; for (int i=0; i<n/2; i++) { Value t = AS_LIST(l)->items[i]; AS_LIST(l)->items[i] = AS_LIST(l)->items[n-1-i]; AS_LIST(l)->items[n-1-i] = t; } }
    PUSH(l); ip++; DISPATCH();
}

label_OP_FILE_READ: {
    Value p = POP(); if (GET_TYPE(p) == VAL_STRING) {
        FILE *f = fopen(AS_STRING(p), "r"); if (f) { fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET); char *b = malloc(sz+1); fread(b,1,sz,f); b[sz]=0; fclose(f); PUSH(value_string(b)); } else PUSH(value_string(""));
    } else PUSH(value_string(""));
    ip++; DISPATCH();
}
label_OP_FILE_WRITE: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "w"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }
label_OP_FILE_APPEND: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "a"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }

label_OP_FSTRING: {
    int n = bc->code[ip].arg; char *res = strdup(""); for (int i=0; i<n; i++) { Value v = POP(); char *vs = val_to_str(v); char *next = malloc(strlen(res) + strlen(vs) + 1); strcpy(next, vs); strcat(next, res); free(res); res = next; }
    PUSH(value_string(res)); ip++; DISPATCH();
}

label_OP_LIST_COMP: ip++; DISPATCH();

label_OP_THROW: vm_exception_value = POP(); vm_exception_active = 1; DISPATCH();

label_OP_TRY_START: {
    if (hsp >= HANDLER_STACK_SIZE) exit(1);
    handler_stack[hsp].catch_ip = bc->code[ip].arg; handler_stack[hsp].csp = csp; handler_stack[hsp].sp = sp; handler_stack[hsp].shadow_list = let_shadow_list;
    hsp++; ip++; DISPATCH();
}
label_OP_TRY_END: if (hsp > 0) hsp--; ip++; DISPATCH();

label_OP_LAMBDA: {
    int pc = bc->code[ip].arg; Bytecode *body = (Bytecode*)bc->code[ip].ptr; char **pnames = malloc(sizeof(char*)*(pc>0?pc:1));
    for (int i=pc-1; i>=0; i--) pnames[i] = (char*)AS_STRING(POP()); Closure *c = closure_new(pc, pnames, body); free(pnames);
    int cnt = 0; for (LetShadow *s = let_shadow_list; s; s = s->next) cnt++;
    if (cnt > 0) { c->captured_count = cnt; c->captured_names = malloc(sizeof(const char*)*cnt); c->captured_values = malloc(sizeof(Value)*cnt); int i=0; for (LetShadow *s = let_shadow_list; s; s = s->next) { c->captured_names[i] = s->name; c->captured_values[i] = var_get(s->name); i++; } }
    PUSH(value_closure(c)); ip++; DISPATCH();
}

label_OP_CALL_VALUE: {
    Value callee = POP(); if (GET_TYPE(callee) == VAL_CLOSURE) {
        Closure *c = AS_CLOSURE(callee); if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
        for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
        Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
        for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
        cf->param_count = c->param_count < 2 ? c->param_count : 2;
        if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
        if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
        for (int i=0; i<c->param_count; i++) var_set_let(c->params[i], ab[i]);
        if (c->param_count > 8) free(ab);
        
        Value rv = run_bytecode(c->body, 0);
        
        csp--;
        cf = &call_stack[csp];
        var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else if (GET_TYPE(callee) == VAL_STRING) {
        FuncEntry *fn = func_get(AS_STRING(callee)); if (!fn) { PUSH(value_none()); ip++; DISPATCH(); }
        if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = fn; cf->pending_closure = NULL;
        cf->local_count = fn->local_count; if (cf->local_count > 0) {
            if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) { cf->local_values[i] = value_none(); } }
            else cf->local_values = calloc(cf->local_count, sizeof(Value));
        } else cf->local_values = NULL;
        if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
        for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
        
        Value rv = run_bytecode(fn->body, 0);
        
        csp--;
        cf = &call_stack[csp];
        if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        if (cf->local_values && cf->local_count > 16) free(cf->local_values);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else { PUSH(value_none()); ip++; DISPATCH(); }
}

label_EXIT_VM:
    return value_none();
}
