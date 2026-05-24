import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Add forward declarations if missing
fw_decls = """
static void print_value(Value v);
static Value run_bytecode(Bytecode *bc, int base_offset);
static void jit_compile(Bytecode *bc);
extern void lsp_run_socket(int port);
"""
if "static void print_value(Value v);" not in text:
    text = text.replace("/* Forward declarations (functions call each other recursively) */", 
                        "/* Forward declarations (functions call each other recursively) */\n" + fw_decls)

# 2. Add print_value implementation if missing entirely
print_val_impl = """
static void print_value(Value v) {
    if (v == 0) { printf("none"); return; }
    ValueType t = GET_TYPE(v);
    if (t == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (t == VAL_FLOAT) printf("%g", AS_FLOAT(v));
    else if (t == VAL_BOOL) printf("%s", AS_BOOL(v) ? "true" : "false");
    else if (t == VAL_STRING) printf("%s", AS_STRING(v));
    else if (t == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) printf("<class %s>", AS_CLASS(v)->name);
        else if (ot == OBJ_TRAIT) printf("<trait %s>", AS_TRAIT(v)->name);
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
        else if (ot == OBJ_BOUND_METHOD) printf("<bound method %s>", AS_BOUND_METHOD(v)->method->name);
        else if (ot == OBJ_NATIVE_FUNC) printf("<native function %s>", AS_NATIVE_FUNC(v)->name);
        else printf("<object>");
    }
    else if (t == VAL_LIST) {
        printf("[");
        for (int j = 0; j < AS_LIST(v)->size; j++) {
            print_value(AS_LIST(v)->items[j]);
            if (j < AS_LIST(v)->size - 1) printf(", ");
        }
        printf("]");
    }
}
"""

if "static void print_value(Value v)" not in text:
    # Insert right before val_to_str
    text = text.replace("static char *val_to_str(Value v) {", print_val_impl + "\nstatic char *val_to_str(Value v) {")

# 3. Find duplicate thread_worker and remove the first one
parts = text.split("static void* thread_worker(void *arg) {")
if len(parts) > 2:
    # We have duplicates.
    # The first implementation is usually the broken one at the top.
    # We'll piece it back together by removing the first body.
    # Find the end of the first body (a closing brace '}')
    first_body_end = parts[1].find("}\n") + 2
    text = parts[0] + parts[1][first_body_end:] + "static void* thread_worker(void *arg) {" + parts[2]

# 4. Remove duplicate CallFrame and ThreadData definitions if they appear inside the grammar rules.
# They shouldn't be there.
# Let's write the modified text back.
with open("src/glowlang.y", "w") as f:
    f.write(text)
