with open("src/glowlang.y", "r") as f:
    text = f.read()

constructors = """
static Node *newTryCatchNode(Node *try_body, const char *err_var, Node *catch_body) { Node *n = node_new(NODE_TRY_CATCH); n->left = try_body; n->name = err_var; n->right = catch_body; return n; }
static Node *newThrowNode(Node *expr) { Node *n = node_new(NODE_THROW); n->left = expr; return n; }
"""
text = text.replace("static Node* newListLiteral", constructors + "static Node* newListLiteral")

with open("src/glowlang.y", "w") as f:
    f.write(text)
