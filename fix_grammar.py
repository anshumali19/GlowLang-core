import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Refine arg_list to avoid conflict
text = text.replace("arg_list: expr_list { $$ = $1; } | { $$.nodes = NULL; $$.count = 0; };", "arg_list: expr_list { $$ = $1; };")
# 2. Postfix rule already handles empty case: | postfix '(' ')' { $$ = newCallExprNode($1, NULL, 0); }
# So the nullable arg_list was indeed redundant.

with open("src/glowlang.y", "w") as f:
    f.write(text)
