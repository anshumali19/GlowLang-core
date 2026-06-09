with open("src/compiler.c", "r") as f:
    content = f.read()
content = content.replace("for (int i = 0; i < local_count; i++) {", "for (int i = 0; i < 10; i++) {")
with open("src/compiler.c", "w") as f:
    f.write(content)
