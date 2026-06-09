import re

with open('glowlang.y', 'r') as f:
    text = f.read()

fields = ['intValue', 'floatValue', 'boolValue', 'stringValue', 'listValue', 'dictValue', 'closureValue']
for field in fields:
    # Match something like `a.intValue` or `l->items[i].intValue`
    pattern = r'([a-zA-Z0-9_]+(?:\[[^\]]+\])?(?:->[a-zA-Z0-9_]+(?:\[[^\]]+\])?)*)\.' + field
    func = 'as_' + field.replace('Value', '').lower()
    text = re.sub(pattern, rf'{func}(\1)', text)

# Special handling for n->value.intValue -> as_int(n->value)
text = re.sub(r'([a-zA-Z0-9_]+->value)\.([a-zA-Z_]+)', lambda m: f"as_{m.group(2).replace('Value','').lower()}({m.group(1)})", text)

with open('glowlang.y', 'w') as f:
    f.write(text)
