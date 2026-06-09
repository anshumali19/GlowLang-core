import os

files_to_patch = [
    "src/gc.c",
    "src/vm.c",
    "src/glowlang.h",
    "src/stdlib.c",
    "src/lsp.c",
    "src/glowlang.y",
    "tools/gdu/gdu.c",
    "tools/debugger/glow_debugger.c",
    "tools/inspector/glow_inspector.c",
    "tools/profiler/glow_profiler.c"
]

for file in files_to_patch:
    if not os.path.exists(file): continue
    with open(file, "r") as f:
        content = f.read()

    # Patch pthread.h
    content = content.replace("#include <pthread.h>", "#ifdef _WIN32\n#include \"win_pthread.h\"\n#else\n#include <pthread.h>\n#endif")

    # Patch unistd.h
    content = content.replace("#include <unistd.h>", "#ifdef _WIN32\n#include <io.h>\n#define access _access\n#else\n#include <unistd.h>\n#endif")

    with open(file, "w") as f:
        f.write(content)

