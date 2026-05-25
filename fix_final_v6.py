import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Restore complete StdLib
std_init_full = """
static Value glow_len(int ac, Value *as) {
    if(ac>0 && GET_TYPE(as[0])==VAL_LIST) return value_int(AS_LIST(as[0])->size);
    if(ac>0 && GET_TYPE(as[0])==VAL_STRING) return value_int(strlen(AS_STRING(as[0])));
    return value_int(0);
}
static Value glow_range(int ac, Value *as) {
    int n = (ac > 0) ? (int)AS_INT(as[0]) : 0;
    List *l = list_new(n);
    for(int i=0; i<n; i++) list_push(l, value_int(i));
    return value_list(l);
}
static Value glow_type(int ac, Value *as) {
    if (ac == 0) return value_string_copy("none");
    ValueType t = GET_TYPE(as[0]);
    if (t == VAL_INT) return value_string_copy("int");
    if (t == VAL_FLOAT) return value_string_copy("float");
    if (t == VAL_BOOL) return value_string_copy("bool");
    if (t == VAL_STRING) return value_string_copy("string");
    if (t == VAL_LIST) return value_string_copy("list");
    if (t == VAL_DICT) return value_string_copy("dict");
    return value_string_copy("object");
}

static void std_init() {
    var_set("len", native_func_new("len", glow_len));
    var_set("range", native_func_new("range", glow_range));
    var_set("type", native_func_new("type", glow_type));
    
    Dict *os = AS_DICT(value_dict_new(8));
    dict_set(os, "exit", native_func_new("exit", os_exit));
    dict_set(os, "time", native_func_new("time", os_time));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));
}
"""
text = re.sub(r"static void std_init\(\) \{.*?\}", std_init_full, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
