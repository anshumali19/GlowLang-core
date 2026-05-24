import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

impls = """
static Node *node_new(NodeType type) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    n->line = line_num;
    return n;
}

static int ast_contains_let(Node *n) {
    if (!n) return 0;
    if (n->type == NODE_LET) return 1;
    if (n->type == NODE_FUNC_DEF) return 0;
    if (n->type == NODE_TRY_CATCH) return ast_contains_let(n->left) || ast_contains_let(n->right) || ast_contains_let(n->third);
    int res = ast_contains_let(n->left) || ast_contains_let(n->right) || ast_contains_let(n->third);
    if (!res && n->elements) for (int i = 0; i < n->element_count; i++) if (ast_contains_let(n->elements[i])) return 1;
    if (!res && n->dict_values) for (int i = 0; i < n->dict_count; i++) if (ast_contains_let(n->dict_values[i])) return 1;
    if (!res && n->call_args) for (int i = 0; i < n->call_arg_count; i++) if (ast_contains_let(n->call_args[i])) return 1;
    if (!res && n->print_items) for (int i = 0; i < n->print_count; i++) if (ast_contains_let(n->print_items[i])) return 1;
    return res;
}
"""
text = text.replace("static Node *newIntNode(int64_t v) {", impls + "\nstatic Node *newIntNode(int64_t v) {")

with open("src/glowlang.y", "w") as f:
    f.write(text)
