import os

def fix():
    with open("src/glowlang.y", "r") as f:
        lines = f.readlines()
    
    new_lines = []
    skip = False
    for line in lines:
        if 'l_OP_TYPE: pc++; DISPATCH();' in line:
            # Add missing labels and remove duplicates
            new_lines.append('    l_OP_FUNC_DEF: pc++; DISPATCH();\n')
            new_lines.append('    l_OP_SKIP: pc++; DISPATCH();\n')
            new_lines.append('    l_OP_TYPE: pc++; DISPATCH(); l_OP_APPEND: pc++; DISPATCH(); l_OP_INPUT: pc++; DISPATCH(); l_OP_WRITE: pc++; DISPATCH(); l_OP_DICT_LITERAL: pc++; DISPATCH(); l_OP_SLICE: pc++; DISPATCH(); l_OP_FOR_EACH: pc++; DISPATCH(); l_OP_FOR_EACH_PAIR: pc++; DISPATCH(); l_OP_PUSH_SCOPE: pc++; DISPATCH(); l_OP_POP_SCOPE: pc++; DISPATCH(); l_OP_LOAD_LOCAL: pc++; DISPATCH(); l_OP_STORE_LOCAL: pc++; DISPATCH(); l_OP_DICT_KEYS: pc++; DISPATCH(); l_OP_DICT_VALUES: pc++; DISPATCH(); l_OP_DICT_HAS: pc++; DISPATCH(); l_OP_DICT_REMOVE: pc++; DISPATCH(); l_OP_DICT_MERGE: pc++; DISPATCH(); l_OP_MATH_ABS: pc++; DISPATCH(); l_OP_MATH_MIN: pc++; DISPATCH(); l_OP_MATH_MAX: pc++; DISPATCH(); l_OP_MATH_ROUND: pc++; DISPATCH(); l_OP_MATH_FLOOR: pc++; DISPATCH(); l_OP_MATH_CEIL: pc++; DISPATCH(); l_OP_MATH_SQRT: pc++; DISPATCH(); l_OP_STR_UPPER: pc++; DISPATCH(); l_OP_STR_LOWER: pc++; DISPATCH(); l_OP_STR_TRIM: pc++; DISPATCH(); l_OP_STR_FIND: pc++; DISPATCH(); l_OP_STR_REPLACE: pc++; DISPATCH(); l_OP_STR_STARTSWITH: pc++; DISPATCH(); l_OP_STR_ENDSWITH: pc++; DISPATCH(); l_OP_STR_SPLIT: pc++; DISPATCH(); l_OP_LIST_PUSH: pc++; DISPATCH(); l_OP_LIST_POP: pc++; DISPATCH(); l_OP_LIST_INSERT: pc++; DISPATCH(); l_OP_LIST_REMOVE: pc++; DISPATCH(); l_OP_LIST_CONTAINS: pc++; DISPATCH(); l_OP_LIST_SORT: pc++; DISPATCH(); l_OP_LIST_REVERSE: pc++; DISPATCH(); l_OP_FILE_READ: pc++; DISPATCH(); l_OP_FILE_WRITE: pc++; DISPATCH(); l_OP_FILE_APPEND: pc++; DISPATCH(); l_OP_FSTRING: pc++; DISPATCH(); l_OP_LIST_COMP: pc++; DISPATCH(); l_OP_LAMBDA: pc++; DISPATCH();\n')
            skip = True # Skip the old messy line
            continue
        if skip and 'return value_none();' in line:
            new_lines.append(line)
            skip = False
            continue
        if not skip:
            new_lines.append(line)

    with open("src/glowlang.y", "w") as f:
        f.writelines(new_lines)

fix()
