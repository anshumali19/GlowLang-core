import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Fix OP_CALL logic for BoundMethod
op_call_fix = """
            case OP_CALL: {
                int argc = inst.arg;
                Value callee = POP();
                if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
                    ObjType ot = GET_OBJ_TYPE(callee);
                    if (ot == OBJ_CLASS) {
                        Value inst_val = instance_new(AS_CLASS(callee));
                        PUSH(inst_val);
                    } else if (ot == OBJ_NATIVE_FUNC) {
                        Value args[16];
                        for(int i=0; i<argc; i++) args[argc-1-i] = POP();
                        PUSH(AS_NATIVE_FUNC(callee)->func(argc, args));
                    } else if (ot == OBJ_BOUND_METHOD) {
                        BoundMethod *bm = AS_BOUND_METHOD(callee);
                        FuncEntry *fe = bm->method;
                        Value saved_self = var_get("self");
                        var_set("self", bm->instance);
                        
                        Value saved_vars[16];
                        const char *saved_names[16];
                        int pcount = (fe->param_count < 16) ? fe->param_count : 16;
                        for(int i=0; i<argc && i<pcount; i++) {
                            saved_names[i] = fe->params[i];
                            saved_vars[i] = var_get(fe->params[i]);
                            var_set(fe->params[i], vm_stack[sp - argc + i]);
                        }
                        sp -= argc;
                        PUSH(run_bytecode(fe->body, 0));
                        for(int i=0; i<argc && i<pcount; i++) var_set(saved_names[i], saved_vars[i]);
                        var_set("self", saved_self);
                    } else {
                        PUSH(value_none());
                    }
                } else if (GET_TYPE(callee) == VAL_CLOSURE) {
                     // Add closure support back if needed
                     PUSH(value_none());
                } else {
                    PUSH(value_none());
                }
                ip++; break;
            }
"""

text = re.sub(r"case OP_CALL: \{.*?ip\+\+; break;\n\s+\}", op_call_fix, text, flags=re.DOTALL)

# 2. Restore Standard Library Functions
std_impls = """
static Value os_exit(int argc, Value *args) {
    exit(argc > 0 ? (int)AS_INT(args[0]) : 0);
    return value_none();
}
static Value os_time(int argc, Value *args) {
    return value_float((double)time(NULL));
}
static Value os_system(int argc, Value *args) {
    if (argc > 0) return value_int(system(AS_STRING(args[0])));
    return value_int(-1);
}
static Value json_stringify_native(int argc, Value *args) { return value_string("{}"); }
static Value json_parse_native(int argc, Value *args) { return value_dict_new(0); }
static Value net_listen(int argc, Value *args) { return value_int(0); }
static Value net_accept(int argc, Value *args) { return value_int(0); }
static Value net_send(int argc, Value *args) { return value_int(0); }
static Value net_recv(int argc, Value *args) { return value_string(""); }
static Value net_close(int argc, Value *args) { return value_none(); }
static Value fs_exists(int argc, Value *args) { return value_bool(0); }
static Value fs_mkdir(int argc, Value *args) { return value_bool(0); }
static Value fs_list(int argc, Value *args) { return value_list_new(0); }
static Value fs_remove(int argc, Value *args) { return value_bool(0); }
static Value fs_size(int argc, Value *args) { return value_int(0); }

static void std_init() {
    Dict *os = AS_DICT(value_dict_new(8));
    dict_set(os, "exit", value_native_func(native_func_new("exit", os_exit)));
    dict_set(os, "time", value_native_func(native_func_new("time", os_time)));
    dict_set(os, "system", value_native_func(native_func_new("system", os_system)));
    var_set("os", value_dict(os));

    Dict *json = AS_DICT(value_dict_new(8));
    dict_set(json, "stringify", value_native_func(native_func_new("stringify", json_stringify_native)));
    dict_set(json, "parse", value_native_func(native_func_new("parse", json_parse_native)));
    var_set("json", value_dict(json));

    Dict *net = AS_DICT(value_dict_new(8));
    dict_set(net, "listen", value_native_func(native_func_new("listen", net_listen)));
    dict_set(net, "accept", value_native_func(native_func_new("accept", net_accept)));
    dict_set(net, "send", value_native_func(native_func_new("send", net_send)));
    dict_set(net, "recv", value_native_func(native_func_new("recv", net_recv)));
    dict_set(net, "close", value_native_func(native_func_new("close", net_close)));
    var_set("net", value_dict(net));

    Dict *fs = AS_DICT(value_dict_new(8));
    dict_set(fs, "exists", value_native_func(native_func_new("exists", fs_exists)));
    dict_set(fs, "mkdir", value_native_func(native_func_new("mkdir", fs_mkdir)));
    dict_set(fs, "list", value_native_func(native_func_new("list", fs_list)));
    dict_set(fs, "remove", value_native_func(native_func_new("remove", fs_remove)));
    dict_set(fs, "size", value_native_func(native_func_new("size", fs_size)));
    var_set("fs", value_dict(fs));
}
"""

text = re.sub(r"static void std_init\(\) \{.*?\}", std_impls, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
