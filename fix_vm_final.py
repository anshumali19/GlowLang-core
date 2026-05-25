import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Fix run_bytecode recursive calls
text = text.replace("run_bytecode(bm->method->body)", "run_bytecode(bm->method->body, 0)")

# 2. Fix StdLib registrations (remove double wrapping)
text = text.replace("value_native_func(native_func_new", "native_func_new")

# 3. Add more robust error checking in OP_CALL
# Handled in previous generalist run? Let's check.
# Actually I'll just rewrite OP_CALL one more time to be safe.
op_call_fix = """
            case OP_CALL: {
                int ac = inst.arg; Value callee = var_get((const char*)inst.ptr);
                if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
                    ObjType ot = GET_OBJ_TYPE(callee);
                    if(ot==OBJ_CLASS) { PUSH(value_instance(instance_new(AS_CLASS(callee)))); }
                    else if(ot==OBJ_BOUND_METHOD) {
                        BoundMethod *bm = AS_BOUND_METHOD(callee); 
                        if (bm->method) {
                            Value s = var_get("self"); var_set("self", bm->instance);
                            Value saved[16];
                            for(int i=0; i<bm->method->param_count && i<16; i++) {
                                saved[i] = var_get(bm->method->params[i]);
                                if (sp >= ac) var_set(bm->method->params[i], vm_stack[sp-ac+i]);
                            }
                            sp -= ac; PUSH(run_bytecode(bm->method->body, 0));
                            for(int i=0; i<bm->method->param_count && i<16; i++) var_set(bm->method->params[i], saved[i]);
                            var_set("self", s);
                        } else { sp -= ac; PUSH(value_none()); }
                    } else if(ot==OBJ_NATIVE_FUNC) {
                        NativeFunc *nf = AS_NATIVE_FUNC(callee);
                        PUSH(nf->func(ac, &vm_stack[sp-ac])); sp -= ac;
                    }
                } else { sp -= ac; PUSH(value_none()); }
                ip++; break;
            }
"""
# text = re.sub(r"case OP_CALL: \{.*?ip\+\+; break;\n\s+\}", op_call_fix, text, flags=re.DOTALL)
# The generalist might have used case OP_CALL_VALUE too.

with open("src/glowlang.y", "w") as f:
    f.write(text)
