#ifndef GLOWLANG_JIT_H
#define GLOWLANG_JIT_H

#include "glowlang.h"

void jit_compile(Bytecode *bc);
void jit_compile_trace(Trace *t);
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig);
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature sig);

#endif
