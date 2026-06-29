#ifndef GLOWLANG_JIT_H
#define GLOWLANG_JIT_H

#include "glowlang.h"

void jit_compile(Bytecode *bc);
void jit_compile_trace(Trace *t);
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig);
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature sig);

// Baseline JIT (Tier 1)
void jit_compile_baseline(Bytecode *bc);
void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature sig);

// Tier management
void jit_maybe_tier_up(Bytecode *bc);
void func_maybe_tier_up(FuncEntry *fe);

#endif
