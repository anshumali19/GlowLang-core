/*
 * Standalone Windows JIT codegen verifier for GlowLang.
 *
 * This does NOT link the full runtime (OpenSSL/SQLite are not required). It
 * provides just enough stubs for jit.c to compile and emit native code for a
 * hand-built Bytecode, then dumps the bytes so they can be disassembled and
 * inspected for the Microsoft x64 ABI (shadow space, rcx/rdx/r8/r9 args,
 * callee-saved RSI/RDI preservation).
 *
 * Build (Windows, clang-cl):
 *   clang-cl /c ..\..\src\jit.c /I..\..\src /D_CRT_SECURE_NO_WARNINGS /Fo jit.obj
 *   clang-cl jit_verify.c jit.obj /I..\..\src /D_CRT_SECURE_NO_WARNINGS /Fe jit_verify.exe
 * Run:
 *   jit_verify.exe
 * It writes jit_dump_base.bin / jit_dump_opt.bin which you disassemble with:
 *   llvm-objdump -D -b binary -m i386:x86-64 jit_dump_base.bin
 */
#include "glowlang.h"
#include "jit.h"
#include <string.h>

/* ---- Stubs for symbols jit.c references ---- */
GlobalTable global_table;

FuncEntry* func_get(const char *name) { (void)name; return NULL; }

Value jit_call_helper(struct FuncEntry *fe, int argc, Value *args, struct Instruction *pc) {
    (void)fe;(void)argc;(void)args;(void)pc; return value_int(0);
}
Value jit_call_helper_val(Value callee, int argc, Value *args, struct Instruction *pc) {
    (void)callee;(void)argc;(void)args;(void)pc; return value_int(0);
}
Value jit_self_call(struct FuncEntry *fe, Value *args) { (void)fe;(void)args; return value_int(0); }
Value jit_call_method_helper(Value obj, const char *n, int argc, Value *args, struct Instruction *pc) {
    (void)obj;(void)n;(void)argc;(void)args;(void)pc; return value_int(0);
}
Value jit_get_property_helper(Value o, const char *n, struct Instruction *pc) {
    (void)o;(void)n;(void)pc; return value_int(0);
}
Value jit_set_property_helper(Value o, const char *n, Value v, struct Instruction *pc) {
    (void)o;(void)n;(void)v;(void)pc; return value_int(0);
}
Value jit_int_add(Value a, Value b) { (void)a;(void)b; return value_int(0); }
Value jit_int_sub(Value a, Value b) { (void)a;(void)b; return value_int(0); }
Value jit_int_mul(Value a, Value b) { (void)a;(void)b; return value_int(0); }
Value jit_int_lt(Value a, Value b)  { (void)a;(void)b; return value_bool(0); }
Value jit_int_gt(Value a, Value b)  { (void)a;(void)b; return value_bool(0); }
Value jit_int_lte(Value a, Value b) { (void)a;(void)b; return value_bool(0); }
Value jit_int_gte(Value a, Value b) { (void)a;(void)b; return value_bool(0); }
Value jit_int_eq(Value a, Value b)  { (void)a;(void)b; return value_bool(0); }
Value glow_index_jit(Value coll, Value v_idx) { (void)coll;(void)v_idx; return value_int(0); }
void glow_store_index_jit(Value coll, Value v_idx, Value v_val) { (void)coll;(void)v_idx;(void)v_val; }
void simd_add_helper(Value v_a, Value v_b, Value v_target) { (void)v_a;(void)v_b;(void)v_target; }

TypeSignature compute_signature(Value *regs, int count) { (void)regs;(void)count; return 0; }
void func_add_specialization(struct FuncEntry *fe, TypeSignature sig, void *code, size_t size) {
    (void)fe;(void)sig;(void)code;(void)size;
}

/* ---- Build a representative bytecode and emit JIT code ---- */
static Instruction mk(OpCode op, int r0, int r1, int r2, int arg) {
    Instruction i; memset(&i, 0, sizeof(i));
    i.op = op; i.r0 = r0; i.r1 = r1; i.r2 = r2; i.arg = arg; i.type = 1;
    return i;
}

int main(void) {
    /* main():
     *   let a = 1            CONST_INT r0=0, arg=1
     *   let b = 2            CONST_INT r0=1, arg=2
     *   let c = a + b        ADD_INT  r0=2, r1=0, r2=1   (-> jit_int_add path)
     *   let d = foo(c, 1)    CALL    r0=3, ptr="foo", r1/r2 set, arg=2  (helper path)
     *   let e = obj:m(c)     CALL_METHOD r0=4, ptr="m", r1=2, r2=..., arg=1 (5-arg helper)
     *   return d             RETURN  r0=3
     */
    Instruction code[8];
    int n = 0;
    code[n++] = mk(OP_CONST_INT, 0, 0, 0, 1);
    code[n++] = mk(OP_CONST_INT, 1, 0, 0, 2);
    code[n++] = mk(OP_ADD_INT, 2, 0, 1, 0);
    /* emulate a CALL (helper) using OP_CALL_SELF_FAST -> shim path */
    code[n++] = mk(OP_CALL_SELF_FAST, 3, 2, 0, 1);
    /* CALL_METHOD (5-arg helper) */
    code[n++] = mk(OP_CALL_METHOD, 4, 2, 1, 1);
    code[n++] = mk(OP_RETURN, 3, 0, 0, 0);

    Bytecode bc; memset(&bc, 0, sizeof(bc));
    bc.code = code; bc.count = n; bc.local_count = 8;

    FuncEntry fe; memset(&fe, 0, sizeof(fe));
    fe.name = "testfn"; fe.body = &bc; fe.param_count = 1;

    /* Minimal smoke test first: only CONST_INT + RETURN */
    Instruction minic[2];
    minic[0] = mk(OP_CONST_INT, 0, 0, 0, 42);
    minic[1] = mk(OP_RETURN, 0, 0, 0, 0);
    Bytecode mbc; memset(&mbc, 0, sizeof(mbc));
    mbc.code = minic; mbc.count = 2; mbc.local_count = 4;
    FuncEntry mfe; memset(&mfe, 0, sizeof(mfe));
    mfe.name = "mini"; mfe.body = &mbc; mfe.param_count = 0;
    jit_compile_baseline_ext(&mbc, &mfe, 0);
    fprintf(stderr, "MINI baseline: %s (%zu bytes)\n", mbc.baseline_jit_code ? "OK" : "FAILED", (size_t)mbc.baseline_jit_size);

    /* Bisect: CONST_INT + ADD_INT + RETURN */
    Instruction ac[3];
    ac[0] = mk(OP_CONST_INT, 0, 0, 0, 1);
    ac[1] = mk(OP_CONST_INT, 1, 0, 0, 2);
    ac[2] = mk(OP_ADD_INT, 2, 0, 1, 0);
    Bytecode abc; memset(&abc, 0, sizeof(abc));
    abc.code = ac; abc.count = 3; abc.local_count = 4;
    FuncEntry afe; memset(&afe, 0, sizeof(afe));
    afe.name = "addi"; afe.body = &abc; afe.param_count = 0;
    jit_compile_baseline_ext(&abc, &afe, 0);
    fprintf(stderr, "ADD_INT baseline: %s (%zu bytes)\n", abc.baseline_jit_code ? "OK" : "FAILED", (size_t)abc.baseline_jit_size);

    /* + RETURN */
    Instruction acr[4];
    acr[0] = mk(OP_CONST_INT, 0, 0, 0, 1);
    acr[1] = mk(OP_CONST_INT, 1, 0, 0, 2);
    acr[2] = mk(OP_ADD_INT, 2, 0, 1, 0);
    acr[3] = mk(OP_RETURN, 2, 0, 0, 0);
    Bytecode abcr; memset(&abcr, 0, sizeof(abcr));
    abcr.code = acr; abcr.count = 4; abcr.local_count = 4;
    FuncEntry afer; memset(&afer, 0, sizeof(afer));
    afer.name = "addir"; afer.body = &abcr; afer.param_count = 0;
    jit_compile_baseline_ext(&abcr, &afer, 0);
    fprintf(stderr, "ADD_INT+RET baseline: %s (%zu bytes)\n", abcr.baseline_jit_code ? "OK" : "FAILED", (size_t)abcr.baseline_jit_size);

    /* CALL_SELF_FAST (shim path on Windows) */
    Instruction sf[3];
    sf[0] = mk(OP_CONST_INT, 0, 0, 0, 5);
    sf[1] = mk(OP_CALL_SELF_FAST, 1, 0, 0, 1);
    sf[2] = mk(OP_RETURN, 1, 0, 0, 0);
    Bytecode sbc; memset(&sbc, 0, sizeof(sbc));
    sbc.code = sf; sbc.count = 3; sbc.local_count = 4;
    FuncEntry sfe; memset(&sfe, 0, sizeof(sfe));
    sfe.name = "selffn"; sfe.body = &sbc; sfe.param_count = 1;
    jit_compile_baseline_ext(&sbc, &sfe, 0);
    fprintf(stderr, "CALL_SELF_FAST baseline: %s (%zu bytes)\n", sbc.baseline_jit_code ? "OK" : "FAILED", (size_t)sbc.baseline_jit_size);

    /* CALL_METHOD (5-arg helper path) */
    Instruction cm[3];
    cm[0] = mk(OP_CONST_INT, 0, 0, 0, 7);
    cm[1] = mk(OP_CALL_METHOD, 1, 0, 0, 1);
    cm[2] = mk(OP_RETURN, 1, 0, 0, 0);
    Bytecode cmbc; memset(&cmbc, 0, sizeof(cmbc));
    cmbc.code = cm; cmbc.count = 3; cmbc.local_count = 4;
    FuncEntry cmfe; memset(&cmfe, 0, sizeof(cmfe));
    cmfe.name = "methodfn"; cmfe.body = &cmbc; cmfe.param_count = 1;
    jit_compile_baseline_ext(&cmbc, &cmfe, 0);
    fprintf(stderr, "CALL_METHOD baseline: %s (%zu bytes)\n", cmbc.baseline_jit_code ? "OK" : "FAILED", (size_t)cmbc.baseline_jit_size);

    /* The full 6-op function intentionally mixes self-recursion with a complex
     * call, which the JIT validator rejects (has_self && has_complex). That is
     * correct, expected behavior. We already verified each opcode path emits
     * above. Dump the representative single-feature binaries for disassembly. */
    if (abcr.baseline_jit_code) {
        FILE *f = fopen("jit_dump_add.bin", "wb");
        if (f) { fwrite(abcr.baseline_jit_code, 1, abcr.baseline_jit_size, f); fclose(f); }
    }
    if (cmbc.baseline_jit_code) {
        FILE *f = fopen("jit_dump_method.bin", "wb");
        if (f) { fwrite(cmbc.baseline_jit_code, 1, cmbc.baseline_jit_size, f); fclose(f); }
    }
    if (sbc.baseline_jit_code) {
        FILE *f = fopen("jit_dump_self.bin", "wb");
        if (f) { fwrite(sbc.baseline_jit_code, 1, sbc.baseline_jit_size, f); fclose(f); }
    }

    fprintf(stderr, "\nDumped binaries: jit_dump_add.bin, jit_dump_method.bin, jit_dump_self.bin\n");
    return 0;
}
