with open("src/glowlang.h", "r") as f:
    content = f.read()

content = "#ifdef _MSC_VER\n#define __thread __declspec(thread)\n#endif\n\n" + content

with open("src/glowlang.h", "w") as f:
    f.write(content)

