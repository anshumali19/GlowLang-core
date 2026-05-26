import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Fix OP_GET_PROPERTY
op_get_fix = """
            case OP_GET_PROPERTY: {
                const char *name = (const char*)inst.ptr;
                Value obj = POP();
                if (GET_TYPE(obj) == VAL_OBJECT && (obj & MASK_PAYLOAD)) {
                    ObjType ot = GET_OBJ_TYPE(obj);
                    if (ot == OBJ_INSTANCE) {
                        Value v = instance_get(AS_INSTANCE(obj), name);
                        if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BOUND_METHOD && AS_BOUND_METHOD(v)->instance == value_none())
                            v = bound_method_new(obj, AS_BOUND_METHOD(v)->method);
                        PUSH(v); ip++; break;
                    }
                    if (ot == OBJ_DICT) {
                        PUSH(dict_get(AS_DICT(obj), name)); ip++; break;
                    }
                }
                PUSH(value_none()); ip++; break;
            }
"""
text = re.sub(r"case OP_GET_PROPERTY: \{.*?ip\+\+; break;\n\s+\}", op_get_fix, text, flags=re.DOTALL)

# 2. Fix OP_CALL / OP_CALL_VALUE
op_call_fix = """
            case OP_CALL_VALUE:
            case OP_CALL: {
                int ac = inst.arg; 
                Value callee = (inst.op == OP_CALL) ? var_get((const char*)inst.ptr) : POP();
                if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
                    ObjType ot = GET_OBJ_TYPE(callee);
                    if (ot == OBJ_CLASS) {
                        PUSH(value_instance(instance_new(AS_CLASS(callee))));
                    } else if (ot == OBJ_BOUND_METHOD) {
                        BoundMethod *bm = AS_BOUND_METHOD(callee);
                        Value s = var_get("self"); var_set("self", bm->instance);
                        if (bm->method && bm->method->body) {
                            Value *saved = (bm->method->param_count > 0 && bm->method->params) ? malloc(sizeof(Value) * bm->method->param_count) : NULL;
                            for (int i = 0; i < bm->method->param_count && i < ac; i++) {
                                saved[i] = var_get(bm->method->params[i]);
                                if (sp - ac + i >= 0) var_set(bm->method->params[i], vm_stack[sp - ac + i]);
                            }
                            sp -= ac; PUSH(run_bytecode(bm->method->body, 0));
                            for (int i = 0; i < bm->method->param_count && i < ac; i++) {
                                var_set(bm->method->params[i], saved[i]);
                            }
                            if (saved) free(saved);
                        } else { sp -= ac; PUSH(value_none()); }
                        var_set("self", s);
                    } else if (ot == OBJ_NATIVE_FUNC) {
                        NativeFunc *nf = AS_NATIVE_FUNC(callee);
                        if (sp >= ac) {
                            PUSH(nf->func(ac, &vm_stack[sp - ac]));
                            sp -= ac;
                        } else PUSH(value_none());
                    } else PUSH(value_none());
                } else {
                    FuncEntry *fe = func_get((const char*)inst.ptr);
                    if (fe && fe->body) {
                        Value *saved = (fe->param_count > 0 && fe->params) ? malloc(sizeof(Value) * fe->param_count) : NULL;
                        for (int i = 0; i < fe->param_count && i < ac; i++) {
                            saved[i] = var_get(fe->params[i]);
                            if (sp - ac + i >= 0) var_set(fe->params[i], vm_stack[sp - ac + i]);
                        }
                        sp -= ac; PUSH(run_bytecode(fe->body, 0));
                        for (int i = 0; i < fe->param_count && i < ac; i++) {
                            var_set(fe->params[i], saved[i]);
                        }
                        if (saved) free(saved);
                    } else { sp -= ac; PUSH(value_none()); }
                }
                ip++; break;
            }
"""
text = re.sub(r"case OP_CALL_VALUE:.*?ip\+\+; break;\n\s+\}", op_call_fix, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
