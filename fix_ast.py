import re

ast_impls = """
/* ================================================================
 *  DYNAMIC AST CONSTRUCTORS
 * ================================================================ */
static Node *newIntNode(int64_t v) {
    Node *n = node_new(NODE_INT_LIT); n->value = value_int(v); return n;
}
static Node *newFloatNode(double v) {
    Node *n = node_new(NODE_FLOAT_LIT); n->value = value_float(v); return n;
}
static Node *newBoolNode(int v) {
    Node *n = node_new(NODE_BOOL_LIT); n->value = value_bool(v); return n;
}
static Node *newStringNode(const char *s) {
    Node *n = node_new(NODE_STRING_LIT); n->name = s; return n;
}
static Node *newFStringNode(const char *s) {
    Node *n = node_new(NODE_FSTRING); n->name = s; return n;
}
static Node *newVarNode(const char *s) {
    Node *n = node_new(NODE_VAR); n->name = s; return n;
}
static Node *newAssignNode(const char *s, Node *expr) {
    Node *n = node_new(NODE_ASSIGN); n->name = s; n->left = expr; return n;
}
static Node *newIndexAssignNode(Node *coll, Node *idx, Node *val) {
    Node *n = node_new(NODE_INDEX_ASSIGN); n->left = coll; n->right = idx; n->third = val; return n;
}
static Node *newCompoundNode(NodeType t, const char *s, Node *expr) {
    Node *n = node_new(t); n->name = s; n->left = expr; return n;
}
static Node *newBinOp(NodeType t, Node *l, Node *r) {
    Node *n = node_new(t); n->left = l; n->right = r; return n;
}
static Node *newUnary(NodeType t, Node *l) {
    Node *n = node_new(t); n->left = l; return n;
}
static Node *newIfNode(Node *cond, Node *if_true, Node *if_false) {
    Node *n = node_new(NODE_IF); n->left = cond; n->right = if_true; n->third = if_false; return n;
}
static Node *newWhileNode(Node *cond, Node *body) {
    Node *n = node_new(NODE_WHILE); n->left = cond; n->right = body; return n;
}
static Node *newUntilNode(Node *cond, Node *body) {
    Node *n = node_new(NODE_UNTIL); n->left = cond; n->right = body; return n;
}
static Node *newRepeatNode(Node *count, Node *body) {
    Node *n = node_new(NODE_REPEAT); n->left = count; n->right = body; return n;
}
static Node *newForEachPairNode(const char *v1, const char *v2, Node *coll, Node *body) {
    Node *n = node_new(NODE_FOR_EACH); n->name = v1; n->second_name = v2; n->left = coll; n->right = body; return n;
}
static Node *newForEachNode(const char *v1, Node *coll, Node *body) {
    Node *n = node_new(NODE_FOR_EACH); n->name = v1; n->left = coll; n->right = body; return n;
}
static Node *newTryCatchNode(Node *try_body, const char *err_var, Node *catch_body) {
    Node *n = node_new(NODE_TRY_CATCH); n->left = try_body; n->name = err_var; n->right = catch_body; return n;
}
static Node *newThrowNode(Node *expr) {
    Node *n = node_new(NODE_THROW); n->left = expr; return n;
}
static Node *newExprStmtNode(Node *expr) {
    Node *n = node_new(NODE_EXPR_STMT); n->left = expr; return n;
}
static Node *newIndexNode(Node *coll, Node *idx) {
    Node *n = node_new(NODE_INDEX); n->left = coll; n->right = idx; return n;
}
static Node *newSliceNode(Node *coll, Node *start, Node *end) {
    Node *n = node_new(NODE_SLICE); n->left = coll; n->right = start; n->third = end; return n;
}
static Node *newCallNode(const char *name, Node **args, int count) {
    Node *n = node_new(NODE_CALL); n->name = name; n->call_args = args; n->call_arg_count = count; return n;
}
static Node *newCallExprNode(Node *expr, Node **args, int count) {
    Node *n = node_new(NODE_CALL_EXPR); n->left = expr; n->call_args = args; n->call_arg_count = count; return n;
}
static Node *newLambdaNode(int param_count, char **params, Node *body) {
    Node *n = node_new(NODE_LAMBDA); n->func_param_count = param_count; n->func_params = params; n->left = body; return n;
}
static Node *newListLiteral(Node **elements, int count) {
    Node *n = node_new(NODE_LIST_LITERAL); n->elements = elements; n->element_count = count; return n;
}
static Node *newListCompNode(Node *expr, const char *var, Node *coll, Node *filter) {
    Node *n = node_new(NODE_LIST_COMP); n->comp_expr = expr; n->comp_var = var; n->left = coll; n->comp_filter = filter; return n;
}
static Node *newDictLiteral(Node **keys, Node **values, int count) {
    Node *n = node_new(NODE_DICT_LITERAL); n->dict_keys = keys; n->dict_values = values; n->dict_count = count; return n;
}
static Node *newFuncNode(const char *name, int param_count, char **params, Node *body) {
    Node *n = node_new(NODE_FUNC_DEF); n->name = name; n->func_param_count = param_count; n->func_params = params; n->left = body; return n;
}
static Node *newReturnNode(Node *expr) {
    Node *n = node_new(NODE_RETURN); n->left = expr; return n;
}
static Node *newShowNode(Node **items, int count) {
    Node *n = node_new(NODE_SHOW); n->print_items = items; n->print_count = count; return n;
}
static Node *newWriteNode(Node **items, int count) {
    Node *n = node_new(NODE_WRITE); n->print_items = items; n->print_count = count; return n;
}
static Node *newSeqNode(Node *l, Node *r) {
    Node *n = node_new(NODE_SEQ); n->left = l; n->right = r; return n;
}
"""

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Insert before "/* Compiler context"
text = text.replace("/* Compiler context to track local variables in current function */", ast_impls + "\n/* Compiler context to track local variables in current function */")

with open("src/glowlang.y", "w") as f:
    f.write(text)

