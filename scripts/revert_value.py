import re

with open('glowlang.y', 'r') as f:
    text = f.read()

# Reverse the replacements made earlier
fields = {
    'as_int': 'intValue',
    'as_float': 'floatValue',
    'as_bool': 'boolValue',
    'as_string': 'stringValue',
    'as_list': 'listValue',
    'as_dict': 'dictValue',
    'as_closure': 'closureValue'
}

for func, field in fields.items():
    # Match something like `as_int(v)` -> `v.intValue`
    # Also handle pointers `as_int(n->value)` -> `n->value.intValue`
    pattern = rf'{func}\(([^)]+)\)'
    text = re.sub(pattern, rf'\1.{field}', text)

with open('glowlang.y', 'w') as f:
    f.write(text)
