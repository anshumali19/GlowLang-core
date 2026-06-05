I've completed a deep audit of all four key files in the GlowLang compiler pipeline. Here is my comprehensive findings report. I'll also write it as an artifact at `/home/anshumali/.gemini/antigravity-cli/brain/d2b7710a-dd8c-4120-95eb-e9137d2cc636/glowlang_parser_lexer_audit.md`.

---

# GLOWLANG PARSER & LEXER DEEP AUDIT REPORT

## Files Analyzed
- **glowlang.l** (230 lines) — Lexer
- **glowlang.y** (4146 lines) — Parser + Compiler + VM (monolithic)
- **glowlang.h** (575 lines) — Header/types
- **ast.c** (11 lines) — Empty stub (all AST logic moved into glowlang.y)

---

## ARCHITECTURE CRITICAL FINDING

The `glowlang.y` file is a **monolithic 4146-line monster** that contains:
- Bison grammar rules (lines 1683–1725)
- The entire bytecode compiler (lines 1145–1649)
- The entire VM interpreter (lines 1847–2768)
- All native functions/stdlib (lines 2804–3846)
- The main() entry point (lines 3908–4145)
- GC implementation (lines 501–706)
- JIT helpers (lines 76–160)

This is a **severe** architectural issue — all of these should be in separate compilation units.

---

## FEATURE-BY-FEATURE AUDIT

### 1. when/otherwise (if/else)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `when`→WHEN, `if`→WHEN, `eif`→EIF, `otherwise`→OTHERWISE, `else`→OTHERWISE (L71-75) |
| Parser | ✅ | Full rule at L1702: `WHEN condition COLON NEWLINE lines eif_chain otherwise_part END` |
| AST | ✅ | Uses `newIfNode()` → `NODE_IF` (L912) |
| Compiler | ✅ | `compile_stmt` case NODE_IF (L1185-1194) — correct conditional jump with backpatching |
| Runtime | ✅ | OP_JUMP_IF_FALSE, OP_JUMP fully implemented |
| **Verdict** | **FULLY WORKING** | Including eif (elif) chains |

### 2. every/each (for-each loops)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `for`/`every`→FOR (L82-83), `each`→EACH (L84), `in`→IN (L85) |
| Parser | ✅ | L1693: `FOR EACH ID IN expr COLON NEWLINE lines END` |
| AST | ✅ | NODE_FOR_EACH |
| Compiler | ✅ | L1233-1262 — uses OP_ITER_START + OP_ITER_NEXT loop |
| Runtime | ✅ | OP_ITER_START (L2692-2702), OP_ITER_NEXT (L2704-2755) — works for lists, dicts, SQLite iterators |
| **Verdict** | **FULLY WORKING** | |

### 3. for-range loops
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `from`→FROM (L88), `to`→TO (L89) |
| Parser | ✅ | L1694: `FOR ID FROM expr TO expr COLON NEWLINE lines END` |
| Compiler | ✅ | L1206-1231 — uses global var slot for loop var, LTE condition |
| Runtime | ✅ | Standard jump-based loop |
| **Verdict** | **FULLY WORKING** | But uses global slot even inside functions (minor bug) |

### 4. while loops
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `while`→WHILE (L76) |
| Parser | ✅ | L1703 |
| Compiler | ✅ | L1196-1204 |
| Runtime | ✅ | Standard condition-check loop |
| **Verdict** | **FULLY WORKING** | |

### 5. until loops ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `until`→UNTIL (L77) |
| Parser | ❌ | **Token declared but NO grammar rule uses UNTIL** |
| AST | ⚠️ | NODE_UNTIL declared in header (L396) but never created |
| Compiler | ❌ | No case for NODE_UNTIL in compile_stmt |
| Runtime | N/A | |
| **Verdict** | **BROKEN** | Lexer recognizes "until" but parser has no rule — causes parse error |

### 6. repeat N times ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `repeat`→REPEAT (L78), `times`→TIMES_KW (L79) |
| Parser | ❌ | **Token declared but NO grammar rule uses REPEAT or TIMES_KW** |
| AST | ⚠️ | NODE_REPEAT declared in header (L396) but never created |
| Compiler | ❌ | No case for NODE_REPEAT |
| **Verdict** | **BROKEN** | Same issue — tokens exist, grammar rules don't |

### 7. whenever (reactive)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `whenever`→WHENEVER (L116) |
| Parser | ✅ | L1701: `WHENEVER condition COLON NEWLINE lines END` |
| AST | ✅ | NODE_WHENEVER |
| Compiler | ✅ | L1372-1414 — **Clever**: transforms into a spawned lambda with polling loop (0.01s sleep, edge-triggered) |
| Runtime | ✅ | Via spawn + while-true loop desugaring |
| **Verdict** | **FULLY WORKING** | Implemented as syntactic sugar over spawn. Uses os.sleep polling — not truly reactive but functional |

### 8. use (module system)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `use`→USE (L114) |
| Parser | ⚠️ | Dual meaning: `USE STRING` → import file (L1689), `USE ID` → use trait (L1700) |
| Compiler | ✅ | `USE STRING` → OP_USE_FILE (L1369), `USE ID` → OP_USE_TRAIT (L1334) |
| Runtime | ✅ | OP_USE_FILE (L2599-2643) — reads, parses, compiles, runs inline. OP_USE_TRAIT (L2484-2493) — copies methods |
| **Verdict** | **FULLY WORKING** | But ambiguity: if a variable name matches a file path, parsing could be confused. The `USE STRING` vs `USE ID` split handles it though. |

### 9. import
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `import`→IMPORT (L108) |
| Parser | ✅ | L1688: `IMPORT STRING` |
| Compiler | ✅ | → OP_USE_FILE (L1369) |
| Runtime | ✅ | Same as `use "file"` |
| **Verdict** | **FULLY WORKING** | No duplicate-import guard though — re-runs file every time |

### 10. spawn (concurrency)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `spawn`→SPAWN (L115) |
| Parser | ✅ | L1706: `SPAWN expr` |
| Compiler | ✅ | L1350-1367 — emits OP_SPAWN |
| Runtime | ✅ | L2457-2461 — creates pthread, passes args, uses GIL |
| **Verdict** | **FULLY WORKING** | Uses GIL (not true parallelism). Thread cleanup issues — detached threads with no join. |

### 11. async/await ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ❌ | **No tokens for "async" or "await"** |
| Parser | ❌ | No rules |
| **Verdict** | **NOT IMPLEMENTED** | Not even shell — completely absent from the language |

### 12. closures/lambdas
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `lambda`→LAMBDA (L93) |
| Parser | ✅ | L1722 — 4 variants: with/without params, single-expr and multi-line |
| Compiler | ✅ | L1625-1641 — compiles body, emits OP_LAMBDA |
| Runtime | ✅ | L2551-2588 — creates Closure object with captured variables from parent scope |
| **Verdict** | **FULLY WORKING** | Captures are by-value snapshot (not by-reference) — a design choice, but documented behavior |

### 13. blueprint/class
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `class`/`blueprint`/`Blueprint`→CLASS (L109-111), `extends`→EXTENDS (L113) |
| Parser | ✅ | L1697-1698 — with and without inheritance |
| Compiler | ✅ | L1322-1331 — emits OP_CLASS_DEF + OP_METHOD_DEF for contained functions |
| Runtime | ✅ | L2305-2314 — creates Class + Dict-based method storage, inheritance via parent chain |
| **Verdict** | **FULLY WORKING** | Shape system declared but mostly unused (stable_shape never fully leveraged) |

### 14. actions (methods)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `action`/`can`→FUNC (L91-92) |
| Parser | ✅ | Same as func — methods are detected by being inside a class body |
| Compiler | ✅ | L1292-1294 — auto-adds "self" as first local when inside class/trait |
| Runtime | ✅ | Method dispatch via OP_CALL_VALUE + bound methods |
| **Verdict** | **FULLY WORKING** | |

### 15. trait system
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `trait`→TRAIT (L112) |
| Parser | ✅ | L1699 |
| Compiler | ✅ | L1333 — OP_TRAIT_DEF + OP_USE_TRAIT |
| Runtime | ✅ | L2483-2493 — copies trait methods into class |
| **Verdict** | **FULLY WORKING** | |

### 16. tensors/packed arrays
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | N/A | Accessed via stdlib (tensor.create, etc.) |
| Parser | N/A | No special syntax |
| Runtime | ✅ | Full native functions: tensor.create, tensor.zeros, tensor.add, tensor.sub, tensor.mul, tensor.dot, tensor.transpose, tensor.matmul — all with AVX2 SIMD (L3503-3573) |
| **Verdict** | **FULLY WORKING** | Impressive SIMD-accelerated implementation. No syntax sugar though — purely API-based. |

### 17. try/catch/throw
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `try`→TRY, `catch`→CATCH, `throw`→THROW (L105-107) |
| Parser | ✅ | L1704-1705 |
| Compiler | ✅ | L1335-1349 — bc_add_handler for exception table |
| Runtime | ✅ | L2280-2303 — searches exception table, unwinds stack |
| **Verdict** | **FULLY WORKING** | Includes stack trace capture (L2285-2289) |

### 18. f-strings ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | L170-177 — recognizes `f"..."` pattern, returns FSTRING token |
| Parser | ❌ | **FSTRING token is declared (L1666) but NEVER appears in any grammar rule** |
| AST | ⚠️ | NODE_FSTRING declared in header (L409) but never created |
| Compiler | ❌ | No case for NODE_FSTRING in compile_expr or compile_stmt |
| Runtime | ⚠️ | `l_OP_FSTRING: pc++; DISPATCH();` (L2549) — **NOP stub** |
| **Verdict** | **BROKEN** | Lexer tokenizes f-strings but they can't be parsed. OP_FSTRING is a dead NOP. |

### 19. list comprehensions ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Parser | ❌ | **No grammar rule for list comprehensions** |
| AST | ⚠️ | NODE_LIST_COMP declared (L411), Node has comp_expr/comp_var/comp_filter fields (L439-441) |
| Compiler | ❌ | No case for NODE_LIST_COMP in compile_expr |
| Runtime | ⚠️ | `l_OP_LIST_COMP: pc++; DISPATCH();` (L2550) — **NOP stub** |
| **Verdict** | **BROKEN** | Infrastructure exists in header but never connected |

### 20. destructuring
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ❌ | No special tokens |
| Parser | ❌ | No grammar rules |
| **Verdict** | **NOT IMPLEMENTED** | |

### 21. match/case (pattern matching)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ❌ | No tokens for "match" or "case" |
| Parser | ❌ | No grammar rules |
| **Verdict** | **NOT IMPLEMENTED** | |

### 22. input/ask ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `input`/`ask`→INPUT_KW (L80-81) |
| Parser | ❌ | **INPUT_KW is declared but NO grammar rule references it** |
| AST | ⚠️ | NODE_INPUT declared but never created |
| Runtime | ⚠️ | `l_OP_INPUT: pc++; DISPATCH();` (L2494) — **NOP stub** |
| **Verdict** | **BROKEN** | |

### 23. write (no-newline print) ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `write`→WRITE (L70) |
| Parser | ❌ | **No grammar rule for WRITE** (it's a separate token from SHOW) |
| Runtime | ⚠️ | `l_OP_WRITE: pc++; DISPATCH();` (L2495) — **NOP stub** |
| **Verdict** | **BROKEN** | |

### 24. Compound assignment (+=, -=, etc.) ⚠️
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `+=`→PLUS_ASSIGN, `-=`→MINUS_ASSIGN, etc. (L119-123) |
| Parser | ❌ | **Tokens declared but NO grammar rules use them** |
| AST | ⚠️ | NODE_COMPOUND_ADD through NODE_COMPOUND_MOD declared (L391-392) but never created |
| Compiler | ❌ | No cases for NODE_COMPOUND_* |
| **Verdict** | **BROKEN** | Lexer produces tokens that cause parse errors |

### 25. stop/break and skip/continue
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `stop`/`break`→STOP, `skip`/`continue`→SKIP |
| Parser | ✅ | L1691-1692 |
| Compiler | ✅ | L1320-1321 — emits OP_STOP/OP_SKIP |
| Runtime | ⚠️ | `l_OP_STOP: goto l_FINISH;` (L2481) — **exits the entire function, not just the loop**. `l_OP_SKIP: pc++; DISPATCH();` (L2482) — **NOP, doesn't actually skip to loop end** |
| **Verdict** | **BROKEN** | OP_STOP acts like return, not break. OP_SKIP does nothing. |

### 26. is/is not (identity comparison)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `is`→IS, `is not`→IS_NOT (L86-87) |
| Parser | ✅ | L1718 — maps to NODE_EQ and NODE_NEQ |
| Runtime | ✅ | Same as == and != |
| **Verdict** | **FULLY WORKING** | But semantically wrong — `is` should be identity comparison, not equality |

### 27. let (immutable binding)
| Stage | Status | Details |
|-------|--------|---------|
| Lexer | ✅ | `let`→LET (L94) |
| Parser | ✅ | L1707 |
| Compiler | ✅ | L1172-1183 — uses OP_STORE_LET |
| Runtime | ⚠️ | `l_OP_STORE_LET` (L2273-2277) — **identical to OP_STORE_VAR**. No immutability enforcement. |
| **Verdict** | **PARTIALLY WORKING** | Parses and stores, but `let` binding is NOT actually immutable |

---

## DEAD/NOP OPCODES IN VM

The following opcodes exist in the dispatch table but are **NOPs** (just `pc++; DISPATCH();`):

| Opcode | Line | Status |
|--------|------|--------|
| OP_INPUT | 2494 | Dead NOP |
| OP_WRITE | 2495 | Dead NOP |
| OP_SLICE | 2512 | Dead NOP |
| OP_FOR_EACH | 2513 | Dead NOP (replaced by ITER_START/ITER_NEXT) |
| OP_FOR_EACH_PAIR | 2514 | Dead NOP |
| OP_PUSH_SCOPE | 2515 | Dead NOP |
| OP_POP_SCOPE | 2516 | Dead NOP |
| OP_LOAD_LOCAL | 2517 | Dead NOP (replaced by register-based OP_MOVE) |
| OP_STORE_LOCAL | 2518 | Dead NOP |
| OP_DICT_KEYS | 2519 | Dead NOP |
| OP_DICT_VALUES | 2520 | Dead NOP |
| OP_DICT_HAS | 2521 | Dead NOP |
| OP_DICT_REMOVE | 2522 | Dead NOP |
| OP_DICT_MERGE | 2523 | Dead NOP |
| OP_MATH_ABS | 2524 | Dead NOP |
| OP_MATH_MIN–OP_MATH_SQRT | 2525-2530 | Dead NOPs |
| OP_STR_UPPER–OP_STR_SPLIT | 2531-2538 | Dead NOPs |
| OP_LIST_PUSH–OP_LIST_REVERSE | 2539-2545 | Dead NOPs |
| OP_FILE_READ–OP_FILE_APPEND | 2546-2548 | Dead NOPs |
| OP_FSTRING | 2549 | Dead NOP |
| OP_LIST_COMP | 2550 | Dead NOP |
| OP_PACKED_MUL–OP_MATRIX_MUL | 2687-2691 | Dead NOPs (fall through to OP_ITER_START!) |

**That's ~40 dead NOPs.** These were likely from an earlier stack-based VM design that was replaced with the current register-based approach. The builtins (math, string, list, dict, file ops) are now provided as native functions via the stdlib, making these opcodes vestigial.

---

## DEAD TOKEN/NODE TYPES (Declared but never used)

| Item | Declared | Used? |
|------|----------|-------|
| UNTIL token | lexer L77 | ❌ No grammar rule |
| REPEAT token | lexer L78 | ❌ No grammar rule |
| TIMES_KW token | lexer L79 | ❌ No grammar rule |
| INPUT_KW token | lexer L80-81 | ❌ No grammar rule |
| WRITE token | lexer L70 | ❌ No grammar rule |
| PLUS_ASSIGN token | lexer L119 | ❌ No grammar rule |
| MINUS_ASSIGN token | lexer L120 | ❌ No grammar rule |
| MUL_ASSIGN token | lexer L121 | ❌ No grammar rule |
| DIV_ASSIGN token | lexer L122 | ❌ No grammar rule |
| MOD_ASSIGN token | lexer L123 | ❌ No grammar rule |
| FSTRING token | lexer L170-177 | ❌ No grammar rule |
| NODE_UNTIL | header L396 | ❌ Never created |
| NODE_REPEAT | header L396 | ❌ Never created |
| NODE_FSTRING | header L409 | ❌ Never created |
| NODE_LIST_COMP | header L411 | ❌ Never created |
| NODE_IN | header L416 | ❌ Never created |
| NODE_WRITE | header L404 | ❌ Never created |
| NODE_SLICE | header L403 | ❌ Never created |
| NODE_INPUT | header L405 | ❌ Never created |
| NODE_PACKED_ADD | header L421 | ❌ Never created |
| NODE_PACKED_SUB | header L422 | ❌ Never created |
| NODE_COMPOUND_ADD–MOD | header L391-392 | ❌ Never created |
| NODE_NEG | header L393 | ❌ Never created (unary minus not in grammar) |
| NODE_CALL | header L401 | ⚠️ Declared but parser creates NODE_CALL_EXPR instead |

---

## GRAMMAR QUALITY ANALYSIS

### Grammar conflicts
The grammar is **surprisingly clean** for its size. Key potential issues:

1. **Ambiguity in `USE`**: `USE STRING` (import) vs `USE ID` (trait). This works because STRING and ID are different tokens, but if someone writes `use myTrait` it's `USE ID` (trait), while `use "file.glow"` is `USE STRING` (import). This is correct but confusing.

2. **`stmt: expr` fallthrough**: The last rule `| expr { $$ = newExprStmtNode($1); }` is a catch-all that could mask errors.

3. **eif_chain with otherwise**: L1702 has `$7 ? newSeqNode($6, $7) : $6` — if there are eif chains but no otherwise, only eif_chain is used as the else. This is correct.

4. **No precedence for `NOT_OP`**: L1718 uses `NOT_OP term` which gives NOT the same precedence as multiplication rather than making it a unary prefix with highest precedence. Declared as `%nonassoc NOT_OP` (L1681) but the rule bypasses this.

### Error Recovery
- **None.** There is no `error` token in any grammar rule. Parse errors are fatal with a single `yyerror()` message.

---

## CRITICAL BUGS FOUND

### 1. OP_STOP acts as "return", not "break" (L2481)
```c
l_OP_STOP: goto l_FINISH;
```
This exits the entire `run_bytecode()` call, not just the enclosing loop. In a `while` loop inside a function, `stop` will terminate the function entirely.

### 2. OP_SKIP is a NOP (L2482)
```c
l_OP_SKIP: pc++; DISPATCH();
```
`skip`/`continue` does nothing — it just advances to the next instruction instead of jumping to the loop head.

### 3. Debug printf left in production code (L2318)
```c
printf("DEBUG INTERP GET_PROPERTY: prop=%s...");
```
This will print debug output for every single property access in any GlowLang program.

### 4. Unary minus not in grammar
There is no parser rule for `-expr`. The NODE_NEG type exists but can never be created. Users cannot write `-x`.

### 5. `func_table_add` bug (L373)
```c
if (!func_table[idx].used) func_table[idx].used = 1; func_table[idx].name = name; return &func_table[idx];
```
Missing braces — `func_table[idx].name = name` and `return` execute unconditionally, not just when `!used`. This means **any hash collision overwrites the existing function**.

### 6. OP_PACKED_MUL through OP_MATRIX_MUL fall through to OP_ITER_START (L2687-2692)
```c
l_OP_PACKED_MUL:
l_OP_PACKED_LOAD:
l_OP_PACKED_STORE:
l_OP_PACKED_FUSED_MUL_ADD:
l_OP_MATRIX_MUL:
l_OP_ITER_START: {
```
If any of these opcodes are ever emitted, they'll execute OP_ITER_START's code, which creates an iterator — completely wrong behavior.

---

## SUMMARY TABLE

| Feature | Classification |
|---------|---------------|
| when/otherwise (if/else/elif) | ✅ FULLY WORKING |
| every/each (for-each) | ✅ FULLY WORKING |
| for-range | ✅ FULLY WORKING |
| while | ✅ FULLY WORKING |
| whenever (reactive) | ✅ FULLY WORKING |
| func/action/can | ✅ FULLY WORKING |
| lambda/closures | ✅ FULLY WORKING |
| blueprint/class | ✅ FULLY WORKING |
| trait/use trait | ✅ FULLY WORKING |
| try/catch/throw | ✅ FULLY WORKING |
| import/use file | ✅ FULLY WORKING |
| spawn | ✅ FULLY WORKING |
| tensors (PackedArray) | ✅ FULLY WORKING (API-based) |
| show/print | ✅ FULLY WORKING |
| let (immutable) | ⚠️ PARTIALLY WORKING (not enforced) |
| is/is not | ⚠️ PARTIALLY WORKING (is == not identity) |
| stop/break | 🔴 BROKEN (acts as return) |
| skip/continue | 🔴 BROKEN (NOP) |
| until loops | 🔴 BROKEN (no grammar rule) |
| repeat N times | 🔴 BROKEN (no grammar rule) |
| f-strings | 🔴 BROKEN (no grammar rule) |
| list comprehensions | 🔴 BROKEN (no grammar rule) |
| compound assignment (+=) | 🔴 BROKEN (no grammar rule) |
| input/ask | 🔴 BROKEN (no grammar rule) |
| write (no-newline) | 🔴 BROKEN (no grammar rule) |
| unary minus | 🔴 BROKEN (no grammar rule) |
| async/await | ❌ NOT IMPLEMENTED |
| destructuring | ❌ NOT IMPLEMENTED |
| match/case | ❌ NOT IMPLEMENTED |

**Bottom line: 13 features fully working, 2 partially working, 10 broken (lexer/header infrastructure exists but parser rules missing), 3 not implemented at all. Plus ~40 dead NOP opcodes in the VM and a critical `stop`/`skip` bug.**