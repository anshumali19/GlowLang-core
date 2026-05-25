import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Fix instance_get to handle NULL fields
get_fix = """
static Value instance_get(Instance *inst, const char *name) {
    if (!inst) return value_none();
    Shape *s = inst->shape;
    while (s && s->name) {
        if (strcmp(s->name, name) == 0) {
            if (inst->fields && s->offset >= 0) return inst->fields[s->offset];
            return value_none();
        }
        s = s->parent;
    }
    return class_get_method(inst->klass, name);
}
"""
text = re.sub(r"static Value instance_get\(Instance \*inst, const char \*name\) \{.*?\}", get_fix, text, flags=re.DOTALL)

# Fix class_get_method
method_fix = """
static Value class_get_method(Class *klass, const char *name) {
    while (klass) {
        if (klass->methods) {
            Value m = dict_get(klass->methods, name);
            if (GET_TYPE(m) != VAL_NONE) return m;
        }
        klass = klass->parent;
    }
    return value_none();
}
"""
text = re.sub(r"static Value class_get_method\(Class \*klass, const char \*name\) \{.*?\}", method_fix, text, flags=re.DOTALL)

# Fix OP_GET_PROPERTY switch case
op_get_fix = """
            case OP_GET_PROPERTY: {
                const char *name = (const char*)inst.ptr;
                Value obj = POP();
                if (GET_TYPE(obj) == VAL_OBJECT && (obj & MASK_PAYLOAD)) {
                    ObjType ot = GET_OBJ_TYPE(obj);
                    if (ot == OBJ_INSTANCE) {
                        Value v = instance_get(AS_INSTANCE(obj), name);
                        if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BOUND_METHOD) {
                            BoundMethod *bm = AS_BOUND_METHOD(v);
                            if (GET_TYPE(bm->instance) == VAL_NONE) {
                                PUSH(value_bound_method(bound_method_new(obj, bm->method)));
                                ip++; break;
                            }
                        }
                        PUSH(v); ip++; break;
                    }
                }
                PUSH(value_none()); ip++; break;
            }
"""
text = re.sub(r"case OP_GET_PROPERTY: \{.*?ip\+\+; break;\n\s+\}", op_get_fix, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
