import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Expand stmt rules to include TRAIT, TRY, SPAWN, etc.
stmt_patch = """stmt:
      postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else if ($1->type == NODE_INDEX) { if ($1->name) $$ = newIndexAssignNode($1->left, NULL, $3); else $$ = newIndexAssignNode($1->left, $1->right, $3); $$->name = $1->name; } else $$ = $1; }
    | ID PLUS_ASSIGN expr  { $$ = newCompoundNode(NODE_COMPOUND_ADD,  $1, $3); }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | FUNC ID '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->parent_name = $4; $$->left = $7; }
    | TRAIT ID COLON NEWLINE lines END { $$ = node_new(NODE_TRAIT); $$->name = $2; $$->left = $5; }
    | USE ID { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END { $$ = newTryCatchNode($4, $6, $9); }
    | THROW expr { $$ = newThrowNode($2); }
    | SPAWN postfix '(' arg_list ')' { $$ = node_new(NODE_SPAWN); $$->left = newCallExprNode($2, $4.nodes, $4.count); }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); }
;"""
text = re.sub(r"stmt:.*?;\n", stmt_patch, text, flags=re.DOTALL)

# 2. Add missing AST constructors if any (newTryCatchNode, newThrowNode, newForEachNode etc.)
# Check if they exist.
constructors = """
static Node *newTryCatchNode(Node *try_body, const char *err_var, Node *catch_body) { Node *n = node_new(NODE_TRY_CATCH); n->left = try_body; n->name = err_var; n->right = catch_body; return n; }
static Node *newThrowNode(Node *expr) { Node *n = node_new(NODE_THROW); n->left = expr; return n; }
"""
if "newTryCatchNode" not in text:
    text = text.replace("static Node* newListLiteral", constructors + "static Node* newListLiteral")

with open("src/glowlang.y", "w") as f:
    f.write(text)
