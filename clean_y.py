import re

with open("src/glowlang.y", "r") as f:
    content = f.read()

# 1. Remove duplicate lines in l_OP_CALL (saving state)
# We looking for the double save:
# for(int i=0; i<fe->param_count; i++) saved[i] = var_get(fe->params[i]);
# for(int i=0; i<fe->local_count; i++) saved[fe->param_count+i] = var_get(fe->local_names[i]);
# repeat...

double_save = r'for\(int i=0; i<fe->param_count; i\+\+\) saved\[i\] = var_get\(fe->params\[i\]\);\s+for\(int i=0; i<fe->local_count; i\+\+\) saved\[fe->param_count\+i\] = var_get\(fe->local_names\[i\]\);'
content = re.sub(f'({double_save})\\s+{double_save}', r'\1', content)

# 2. Identify all labels defined
labels = re.findall(r'l_OP_[A-Z_]+:', content)
unique_labels = set()
duplicates = []
for l in labels:
    if l in unique_labels:
        duplicates.append(l)
    else:
        unique_labels.add(l)

print(f"Found duplicates: {duplicates}")

# 3. Specifically remove the messy block at the end that causes most issues
# We'll replace it with a clean version containing only labels NOT defined elsewhere.

# Find the start of the "placeholder" block
placeholder_marker = "l_OP_TYPE: pc++; DISPATCH();"
if placeholder_marker in content:
    start_idx = content.find(placeholder_marker)
    end_idx = content.find("return value_none();", start_idx)
    
    if end_idx != -1:
        # Get everything before and after the block
        prefix = content[:start_idx]
        suffix = content[end_idx:]
        
        # Get all labels already defined in the prefix
        existing_labels = set(re.findall(r'l_OP_[A-Z_]+:', prefix))
        
        # All possible opcodes from the dt array
        dt_match = re.search(r'static void\* dt\[\] = \{(.*?)\};', content, re.DOTALL)
        if dt_match:
            all_opcodes = re.findall(r'l_OP_[A-Z_]+', dt_match.group(1))
            missing_labels = []
            for op in all_opcodes:
                label = op + ":"
                if label not in existing_labels:
                    missing_labels.append(label)
            
            # Create a clean placeholder block
            new_block = ""
            for i, label in enumerate(missing_labels):
                new_block += f"    {label} "
                if (i + 1) % 4 == 0:
                    new_block += "pc++; DISPATCH();\n"
                elif i == len(missing_labels) - 1:
                    new_block += "pc++; DISPATCH();\n"
            
            content = prefix + new_block + "    " + suffix

# 4. Final check for any accidental duplicates outside the placeholder block
# (This is rare but good for safety)
lines = content.splitlines()
seen_labels = set()
final_lines = []
for line in lines:
    label_match = re.match(r'^\s*(l_OP_[A-Z_]+:)', line)
    if label_match:
        label = label_match.group(1)
        if label in seen_labels:
            print(f"Removing duplicate label line: {line}")
            continue
        seen_labels.add(label)
    final_lines.append(line)

with open("src/glowlang.y", "w") as f:
    f.write("\n".join(final_lines) + "\n")

