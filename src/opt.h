#ifndef GLOWLANG_OPT_H
#define GLOWLANG_OPT_H

#include "glowlang.h"

// Optimize register-based bytecode using pseudo-SSA passes
void optimize_bytecode(Bytecode *bc);

#endif
