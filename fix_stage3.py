import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# Find the end of the second %{ ... %} block (before the first %%)
# Actually, let's find the FIRST %% and insert before it, but INSIDE a %{ ... %} block or similar.
# Wait, the first %% marks the start of the grammar. Everything before it is declarations.
# If I want to add C functions, they must be in %{ ... %}.

idx_first_pp = text.find("%%")
idx_last_close_brace = text.rfind("%}", 0, idx_first_pp)

constructors = """
static Node *newTryCatchNode(Node *try_body, const char *err_var, Node *catch_body) { Node *n = node_new(NODE_TRY_CATCH); n->left = try_body; n->name = err_var; n->right = catch_body; return n; }
static Node *newThrowNode(Node *expr) { Node *n = node_new(NODE_THROW); n->left = expr; return n; }
"""

# Insert before the last %} of the preamble
text = text[:idx_last_close_brace] + constructors + text[idx_last_close_brace:]

with open("src/glowlang.y", "w") as f:
    f.write(text)
