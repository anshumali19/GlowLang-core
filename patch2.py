with open("src/compiler.c", "r") as f:
    content = f.read()
content = content.replace('printf("   Stack: [");', 'printf("   Stack (base_reg=%d): [", base_reg);')
with open("src/compiler.c", "w") as f:
    f.write(content)
