import glob
for file in ["src/vm.c", "src/stdlib.c", "src/glowlang.y"]:
    try:
        with open(file, "r") as f:
            content = f.read()
        content = content.replace("#include <immintrin.h>", "#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n#include <immintrin.h>\n#endif")
        with open(file, "w") as f:
            f.write(content)
    except Exception as e:
        pass
