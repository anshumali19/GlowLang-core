import re

with open('glowlang.y', 'r') as f:
    text = f.read()

# Known Value variables in glowlang.y
value_vars = [
    r'a', r'b', r'v', r'val', r'old_value', r'vm_return_value', r'vm_exception_value',
    r'l->items\[[^\]]+\]', r'd->values\[[^\]]+\]', r'res->items\[[^\]]+\]',
    r'n->value', r'd1', r'd2', r'src', r'startV', r'endV', r'coll', r'idx', r'p', r't',
    r'key', r'k', r'item', r'lv', r'prompt', r'first', r'best', r'su', r'sep', r'callee',
    r'cf->local_values\[[^\]]+\]', r'var_table\[[^\]]+\].value', r'stack\[[^\]]+\]', r'vm_stack\[[^\]]+\]'
]

# We need a regex that matches any of these variables, followed by .type, .intValue, etc.
# But we must be careful with word boundaries to not match 'va' when we want 'v'.
var_pattern = r'(?:' + '|'.join(rf'\b{v}\b' for v in value_vars) + r')'
# The pattern might have a trailing pointer or array access, so let's be more robust.
# Actually, it's easier to just match:
# (var).type -> GET_TYPE(var)
# (var).intValue -> AS_INT(var)
# etc.

def replacer(m):
    var = m.group(1)
    field = m.group(2)
    if field == 'type': return f'GET_TYPE({var})'
    if field == 'intValue': return f'AS_INT({var})'
    if field == 'floatValue': return f'AS_FLOAT({var})'
    if field == 'boolValue': return f'AS_BOOL({var})'
    if field == 'stringValue': return f'AS_STRING({var})'
    if field == 'listValue': return f'AS_LIST({var})'
    if field == 'dictValue': return f'AS_DICT({var})'
    if field == 'closureValue': return f'AS_CLOSURE({var})'
    return m.group(0)

# We use a pattern that finds something looking like a value variable, and its field.
# But `l->items[i]` is not just a word. Let's just match any `->value.type` or `[i].type`
pattern = r'([a-zA-Z0-9_]+(?:->[a-zA-Z0-9_]+)?(?:\[[^\]]+\])?(?:\.value)?)\.(type|intValue|floatValue|boolValue|stringValue|listValue|dictValue|closureValue)'

# Wait, `n->type` is a NodeType. So if group(1) == 'n' or 'node' or 'left' or 'right' and field == 'type', skip.
def safe_replacer(m):
    var = m.group(1)
    field = m.group(2)
    if field == 'type' and var in ['n', 'node', 'left', 'right', 'third', 'instr', 'bc->code[ip]']:
        return m.group(0)
    # also check if var ends with ->type etc
    return replacer(m)

text = re.sub(pattern, safe_replacer, text)

# Now fix the constructor assignments we found earlier.
# n->value.type = VAL_INT; n->value.intValue = v;
# is already refactored to n->value = value_int(v);

with open('glowlang.y', 'w') as f:
    f.write(text)
