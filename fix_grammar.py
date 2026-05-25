import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Add WHEN, WHILE, UNTIL, REPEAT, FOR, EACH, LET, SPAWN to tokens
text = text.replace("REPEAT FOR", "WHEN EIF OTHERWISE WHILE UNTIL REPEAT FOR")

# 2. Add full stmt rules
stmt_rules = """stmt:
    postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else $$ = newIndexAssignNode($1->left, $1->right, $3); }
    | ID PLUS_ASSIGN expr  { $$ = newCompoundNode(NODE_COMPOUND_ADD,  $1, $3); }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | FUNC ID '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SPAWN postfix '(' arg_list ')' { $$ = node_new(NODE_SPAWN); $$->left = newCallExprNode($2, $4.nodes, $4.count); }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); }
;
"""
text = re.sub(r"stmt:.*?;\n", stmt_rules, text, flags=re.DOTALL)

# 3. Add eif_chain and otherwise_part
eif_rules = """
eif_chain: eif_chain EIF condition COLON NEWLINE lines { $$ = newSeqNode($1, newIfNode($3, $6, NULL)); } | { $$ = NULL; };
otherwise_part: OTHERWISE COLON NEWLINE lines { $$ = $4; } | { $$ = NULL; };
condition: expr;
"""
text = text.replace("eif_chain: /* stub */ { $$ = NULL; };", eif_rules)
text = text.replace("otherwise_part: /* stub */ { $$ = NULL; };", "")

with open("src/glowlang.y", "w") as f:
    f.write(text)
