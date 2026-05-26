import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Add dict_get
dict_get_impl = """
static Value dict_get(Dict *d, const char *key) {
    if (!d) return value_none();
    for (int i = 0; i < d->size; i++) {
        if (strcmp(d->keys[i], key) == 0) return d->values[i];
    }
    return value_none();
}
"""
if "static Value dict_get" not in text:
    # Insert after dict_set closing brace
    # Find dict_set closing brace
    idx = text.find("void dict_set")
    if idx != -1:
        end_idx = text.find("}", idx) + 1
        text = text[:end_idx] + dict_get_impl + text[end_idx:]

# 2. Add native functions if missing
native_impls = """
static Value native_range(int argc, Value *args) {
    int n = (argc > 0) ? (int)AS_INT(args[0]) : 0;
    List *l = list_new(n);
    for (int i = 0; i < n; i++) list_push(l, value_int(i));
    l->size = n;
    return value_list(l);
}
static Value native_type(int argc, Value *args) {
    if (argc < 1) return value_string_copy("none");
    ValueType t = GET_TYPE(args[0]);
    if (t == VAL_INT) return value_string_copy("int");
    if (t == VAL_FLOAT) return value_string_copy("float");
    if (t == VAL_BOOL) return value_string_copy("bool");
    if (t == VAL_STRING) return value_string_copy("string");
    if (t == VAL_LIST) return value_string_copy("list");
    if (t == VAL_DICT) return value_string_copy("dict");
    if (t == VAL_OBJECT) {
        ObjType ot = GET_OBJ_TYPE(args[0]);
        if (ot == OBJ_CLASS) return value_string_copy("class");
        if (ot == OBJ_INSTANCE) return value_string_copy("instance");
    }
    return value_string_copy("object");
}
"""
if "static Value native_range" not in text:
    text = text.replace("static Value native_len", native_impls + "static Value native_len")

# 3. Update std_init
std_init_new = """
static void std_init(void) {
    var_set("len", native_func_new("len", native_len));
    var_set("range", native_func_new("range", native_range));
    var_set("type", native_func_new("type", native_type));
    Dict *os = dict_new(4);
    dict_set(os, "exit", native_func_new("exit", os_exit));
    dict_set(os, "time", native_func_new("time", os_time));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));
    var_set("json", value_dict(dict_new(4)));
    var_set("net", value_dict(dict_new(4)));
    var_set("fs", value_dict(dict_new(4)));
}
"""
text = re.sub(r"static void std_init\(void\) \{.*?\}", std_init_new, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
