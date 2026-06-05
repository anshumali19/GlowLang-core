I'll start by reading all the key files in parallel to perform a comprehensive audit.

=== BLOCK ===

--- THINKING ---
Let me start by reading all the key files to perform a deep audit of the GlowLang runtime system. I'll read multiple files in parallel where possible.

=== BLOCK ===

Created At: 2026-06-04T14:15:10Z
Completed At: 2026-06-04T14:15:11Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.h`
Total Lines: 575
Total Bytes: 14969
Showing lines 1 to 575
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #ifndef GLOWLANG_H
2: #define GLOWLANG_H
3: 
4: #include <stdint.h>
5: #include <string.h>
6: #include <stdio.h>
7: #include <stddef.h>
8: #include <stdbool.h>
9: #include "arena.h"
10: 
11: /* ================================================================
12:  *  VALUE TYPES
13:  * ================================================================ */
14: typedef enum {
15:     VAL_INT=0, VAL_FLOAT=1, VAL_BOOL=2, VAL_STRING=3, VAL_LIST=4, VAL_DICT=5, VAL_OBJECT=6, VAL_NONE=7, VAL_DEOPT=15, VAL_TRACE_EXIT=16
16: } ValueType;
17: 
18: 
19: typedef uint64_t Value;
20: 
21: 
22: /* Forward declarations */
23: typedef struct List List;
24: typedef struct Dict Dict;
25: typedef struct Closure Closure;
26: typedef struct Class Class;
27: typedef struct Instance Instance;
28: typedef struct BoundMethod BoundMethod;
29: typedef struct Trait Trait;
30: typedef struct NativeFunc NativeFunc;
31: typedef struct Shape Shape;
32: typedef struct Node Node;
33: typedef struct Bytecode Bytecode;
34: typedef struct Instruction Instruction;
35: typedef struct FuncEntry FuncEntry;
36: 
37: /* ================================================================
38:  *  GC & OBJECTS
39:  * ================================================================ */
40: typedef enum {
41:     OBJ_STRING, OBJ_ROPE, OBJ_LIST, OBJ_DICT, OBJ_CLOSURE, OBJ_CLASS, OBJ_INSTANCE, OBJ_BOUND_METHOD, OBJ_SHAPE, OBJ_TRAIT, OBJ_NATIVE_FUNC, OBJ_PACKED_ARRAY, OBJ_ITERATOR, OBJ_STRING_BUILDER, OBJ_SQLITE_ITER, OBJ_SQLITE_HANDLE
42: } ObjType;
43: 
44: typedef struct GCObject {
45:     struct GCObject *next;
46:     unsigned char type;
47:     unsigned char marked;
48:     unsigned char is_block_allocated;
49:     unsigned char generation;
50:     unsigned char in_remembered_set;
51:     unsigned char is_stack_allocated;
52:     void *block_ptr;
53:     size_t sz;
54: } GCObject;
55: 
56: typedef struct {
57:     GCObject obj;
58:     void *db;
59: } SQLiteHandle;
60: 
61: typedef enum {
62:     PACKED_INT64,
63:     PACKED_FLOAT64
64: } PackedType;
65: 
66: typedef struct {
67:     GCObject obj;
68:     void *data;
69:     PackedType type;
70:     int size;
71:     int capacity;
72: } PackedArray;
73: 
74: typedef struct {
75:     GCObject obj;
76:     int size;
77:     char chars[];
78: } GCString;
79: 
80: typedef struct RopeNode {
81:     GCObject obj;
82:     size_t length;
83:     bool is_leaf;
84:     union {
85:         struct {
86:             struct RopeNode *left;
87:             struct RopeNode *right;
88:         } internal;
89:         struct {
90:             Value flat_string;
91:             int offset;
92:         } leaf;
93:     };
94: } RopeNode;
95: 
96: typedef struct {
97:     GCObject obj;
98:     Value iterable;
99:     int index;
100: } Iterator;
101: 
102: typedef struct {
103:     GCObject obj;
104:     void *stmt;
105:     int done;
106: } SQLiteIter;
107: 
108: typedef struct {
109:     GCObject obj;
110:     char *buffer;
111:     int size;
112:     int capacity;
113: } StringBuilder;
114: 
115: #define MASK_NAN      ((uint64_t)0x7FF8000000000000)
116: #define MASK_TYPE     ((uint64_t)0x0007000000000000)
117: #define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)
118: 
119: #define IS_DEOPT(v)      ((v) == (MASK_NAN | ((uint64_t)VAL_DEOPT << 48)))
120: #define IS_TRACE_EXIT(v) ((v) == (MASK_NAN | ((uint64_t)VAL_TRACE_EXIT << 48)))
121: 
122: /* Extract bits to type */
123: static inline ValueType GET_TYPE(Value v) {
124:     return (((v) & MASK_NAN) != MASK_NAN) ? VAL_FLOAT : (ValueType)(((v) & MASK_TYPE) >> 48);
125: }
126: #define GET_OBJ_TYPE(v) (((GCObject*)(uintptr_t)((v) & MASK_PAYLOAD))->type)
127: 
128: static inline size_t rope_length(Value v) {
129:     if (GET_TYPE(v) == VAL_STRING) {
130:         GCObject *obj = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
131:         if (obj->type == OBJ_STRING) return strlen(((GCString*)obj)->chars);
132:         if (obj->type == OBJ_ROPE) return ((RopeNode*)obj)->length;
133:     }
134:     return 0;
135: }
136: 
137: /* Accessors */
138: static inline double AS_FLOAT(Value v) { double d; memcpy(&d, &v, sizeof(v)); return d; }
139: static inline int64_t AS_INT(Value v) {
140:     uint64_t payload = v & MASK_PAYLOAD;
141:     if (payload & 0x0000800000000000ULL) return (int64_t)(payload | 0xFFFF000000000000ULL);
142:     return (int64_t)payload;
143: }
144: static inline int AS_BOOL(Value v) { return (int)(v & MASK_PAYLOAD); }
145: extern const char* rope_flatten(Value v);
146: static inline const char* AS_STRING(Value v) {
147:     if (GET_TYPE(v) != VAL_STRING) return "";
148:     return rope_flatten(v);
149: }
150: static inline List* AS_LIST(Value v) { return (List*)(uintptr_t)(v & MASK_PAYLOAD); }
151: static inline Dict* AS_DICT(Value v) { return (Dict*)(uintptr_t)(v & MASK_PAYLOAD); }
152: static inline Closure* AS_CLOSURE(Value v) { return (Closure*)(uintptr_t)(v & MASK_PAYLOAD); }
153: static inline Class* AS_CLASS(Value v) { return (Class*)(uintptr_t)(v & MASK_PAYLOAD); }
154: static inline Instance* AS_INSTANCE(Value v) { return (Instance*)(uintptr_t)(v & MASK_PAYLOAD); }
155: static inline BoundMethod* AS_BOUND_METHOD(Value v) { return (BoundMethod*)(uintptr_t)(v & MASK_PAYLOAD); }
156: static inline Trait* AS_TRAIT(Value v) { return (Trait*)(uintptr_t)(v & MASK_PAYLOAD); }
157: static inline NativeFunc* AS_NATIVE_FUNC(Value v) { return (NativeFunc*)(uintptr_t)(v & MASK_PAYLOAD); }
158: static inline PackedArray* AS_PACKED_ARRAY(Value v) { return (PackedArray*)(uintptr_t)(v & MASK_PAYLOAD); }
159: 
160: /* Value Constructors */
161: static inline Value make_value(uint64_t type, uint64_t payload) {
162:     Value v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);
163:     return v;
164: }
165: 
166: static inline Value value_int(int64_t v) { return make_value(VAL_INT, (uint64_t)v); }
167: static inline Value value_float(double v) { Value val; memcpy(&val, &v, sizeof(v)); return val; }
168: static inline Value value_bool(int v) { return make_value(VAL_BOOL, v ? 1 : 0); }
169: static inline Value value_list(List *l) { return make_value(VAL_LIST, (uint64_t)(uintptr_t)l); }
170: static inline Value value_packed(PackedArray *pa) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)pa); }
171: static inline Value value_dict(Dict *d) { return make_value(VAL_DICT, (uint64_t)(uintptr_t)d); }
172: static inline Value value_closure(Closure *c) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)c); }
173: static inline Value value_class(Class *c) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)c); }
174: static inline Value value_trait(Trait *t) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)t); }
175: static inline Value value_instance(Instance *i) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)i); }
176: static inline Value value_obj(void *ptr) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)ptr); }
177: static inline Value value_bound_method(BoundMethod *bm) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)bm); }
178: static inline Value value_native_func(NativeFunc *nf) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)nf); }
179: static inline Value value_none(void) { return make_value(VAL_NONE, 0); }
180: 
181: typedef uint64_t TypeSignature;
182: 
183: typedef struct GlobalTable {
184:     const char **names;
185:     Value *values;
186:     int count;
187:     int capacity;
188: } GlobalTable;
189: 
190: extern GlobalTable global_table;
191: int global_get_slot(const char *name);
192: 
193: typedef struct CallFrame {
194:     struct FuncEntry *fe;
195:     struct Instruction *pc;
196: } CallFrame;
197: extern __thread CallFrame call_stack[65536];
198: extern __thread int call_stack_ptr;
199: void call_stack_push(struct FuncEntry *fe, struct Instruction *pc);
200: void call_stack_pop(void);
201: 
202: extern TypeSignature compute_signature(Value *regs, int count);
203: extern Value jit_call_helper(struct FuncEntry *fe, int argc, Value *args, struct Instruction *pc);
204: extern Value jit_call_helper_name(const char *name, int argc, Value *args, struct Instruction *pc);
205: extern Value jit_call_helper_val(Value callee, int argc, Value *args, struct Instruction *pc);
206: extern Value packed_array_get_jit(Value v_pa, Value v_idx);
207: extern void packed_array_set_jit(Value v_pa, Value v_idx, Value v_val);
208: extern void simd_add_helper(Value v_a, Value v_b, Value v_target);
209: extern Value value_from_double(double d);
210: 
211: /* ================================================================
212:  *  BYTECODE
213:  * ================================================================ */
214: typedef enum {
215:     OP_CONST_INT,
216:     OP_CONST_FLOAT,
217:     OP_CONST_STRING,
218:     OP_CONST_BOOL,
219:     OP_LOAD_VAR,
220:     OP_STORE_VAR,
221:     OP_ADD,
222:     OP_SUB,
223:     OP_MUL,
224:     OP_DIV,
225:     OP_MOD,
226:     OP_POW,
227:     OP_NEG,
228:     OP_LT,
229:     OP_GT,
230:     OP_LTE,
231:     OP_GTE,
232:     OP_EQ,
233:     OP_NEQ,
234:     OP_AND,
235:     OP_OR,
236:     OP_NOT,
237:     OP_JUMP,
238:     OP_JUMP_IF_FALSE,
239:     OP_JUMP_IF_TRUE,
240:     OP_LEN,
241:     OP_RANGE,
242:     OP_TYPE,
243:     OP_APPEND,
244:     OP_INPUT,
245:     OP_SHOW,
246:     OP_WRITE,
247:     OP_LIST_LITERAL,
248:     OP_DICT_LITERAL,
249:     OP_INDEX,
250:     OP_SLICE,
251:     OP_STORE_INDEX,
252:     OP_FUNC_DEF,
253:     OP_CALL,
254:     OP_CALL_SELF_FAST,
255:     OP_TAIL_CALL,
256:     OP_RETURN,
257:     OP_FOR_EACH,
258:     OP_FOR_EACH_PAIR,
259:     OP_STOP,
260:     OP_SKIP,
261:     OP_POP,
262:     OP_PUSH_SCOPE,
263:     OP_POP_SCOPE,
264:     OP_STORE_LET,
265:     OP_LOAD_LOCAL,
266:     OP_STORE_LOCAL,
267:     OP_DICT_KEYS,
268:     OP_DICT_VALUES,
269:     OP_DICT_HAS,
270:     OP_DICT_REMOVE,
271:     OP_DICT_MERGE,
272:     OP_MATH_ABS,
273:     OP_MATH_MIN,
274:     OP_MATH_MAX,
275:     OP_MATH_ROUND,
276:     OP_MATH_FLOOR,
277:     OP_MATH_CEIL,
278:     OP_MATH_SQRT,
279:     OP_STR_UPPER,
280:     OP_STR_LOWER,
281:     OP_STR_TRIM,
282:     OP_STR_FIND,
283:     OP_STR_REPLACE,
284:     OP_STR_STARTSWITH,
285:     OP_STR_ENDSWITH,
286:     OP_STR_SPLIT,
287:     OP_LIST_PUSH,
288:     OP_LIST_POP,
289:     OP_LIST_INSERT,
290:     OP_LIST_REMOVE,
291:     OP_LIST_CONTAINS,
292:     OP_LIST_SORT,
293:     OP_LIST_REVERSE,
294:     OP_FILE_READ,
295:     OP_FILE_WRITE,
296:     OP_FILE_APPEND,
297:     OP_FSTRING,
298:     OP_LIST_COMP,
299:     OP_THROW,
300:     OP_TRY_START,
301:     OP_TRY_END,
302:     OP_LAMBDA,
303:     OP_CALL_VALUE,
304:     OP_CLASS_DEF,
305:     OP_METHOD_DEF,
306:     OP_TRAIT_DEF,
307:     OP_USE_TRAIT,
308:     OP_GET_PROPERTY,
309:     OP_SET_PROPERTY,
310:     OP_SPAWN,
311:     OP_MOVE,
312:     OP_NOP,
313:     OP_TRACE_ENTER,
314:     OP_PACKED_ADD,
315:     OP_PACKED_SUB,
316:     OP_PACKED_MUL,
317:     OP_PACKED_LOAD,
318:     OP_PACKED_STORE,
319:     OP_PACKED_FUSED_MUL_ADD,
320:     OP_MATRIX_MUL,
321:     OP_SIMD_ADD,
322:     OP_USE_FILE,
323:     OP_ITER_START,
324:     OP_ITER_NEXT
325: } OpCode;
326: 
327: typedef struct {
328:     void *key;
329:     intptr_t val;
330: } CacheEntry;
331: struct Instruction {
332:     OpCode op;
333:     OpCode original_op;
334:     int arg;
335:     intptr_t ptr;
336:     CacheEntry cache[4];
337:     unsigned char cache_next;
338:     long exec_count;
339:     unsigned short r0, r1, r2;
340:     unsigned char type; // 0=Unknown, 1=Int, 2=Float, 3=String, 4=Bool
341:     unsigned char is_stack_alloc;
342:     unsigned char is_method;
343:     int line;
344:     const char *file;
345:     void *trace;
346: };
347: 
348: typedef struct Trace {
349:     Instruction *instructions;
350:     int *original_offsets;
351:     int count;
352:     int capacity;
353:     void *jit_code;
354:     size_t jit_size;
355:     Instruction *start_pc;
356: } Trace;
357: 
358: typedef struct TraceMap {
359:     Instruction *pc;
360:     Trace *trace;
361:     struct TraceMap *next;
362: } TraceMap;
363: 
364: typedef struct {
365:     int start_pc;
366:     int end_pc;
367:     int handler_pc;
368: } ExceptionEntry;
369: 
370: struct Bytecode {
371:     Instruction *code;
372:     int capacity;
373:     int count;
374:     void *jit_code;
375:     size_t jit_size;
376:     uint32_t *jit_offsets;
377:     int jit_failed;
378:     int local_count;
379:     struct FuncEntry *owner;
380:     ExceptionEntry *exception_table;
381:     int exception_count;
382: };
383: 
384: /* ================================================================
385:  *  NODE TYPES
386:  * ================================================================ */
387: typedef enum {
388:     NODE_INT_LIT, NODE_FLOAT_LIT, NODE_BOOL_LIT, NODE_STRING_LIT,
389:     NODE_VAR,
390:     NODE_ASSIGN,
391:     NODE_COMPOUND_ADD, NODE_COMPOUND_SUB, NODE_COMPOUND_MUL,
392:     NODE_COMPOUND_DIV, NODE_COMPOUND_MOD,
393:     NODE_ADD, NODE_SUB, NODE_MUL, NODE_DIV, NODE_MOD, NODE_POW, NODE_NEG,
394:     NODE_LT, NODE_GT, NODE_LTE, NODE_GTE, NODE_EQ, NODE_NEQ,
395:     NODE_AND, NODE_OR, NODE_NOT,
396:     NODE_IF, NODE_WHILE, NODE_UNTIL, NODE_REPEAT,
397:     NODE_FOR_EACH,
398:     NODE_FOR_RANGE,
399:     NODE_STOP, NODE_SKIP,
400:     NODE_LET,
401:     NODE_FUNC_DEF, NODE_CALL, NODE_RETURN,
402:     NODE_LIST_LITERAL, NODE_DICT_LITERAL,
403:     NODE_INDEX, NODE_SLICE,
404:     NODE_SHOW, NODE_WRITE,
405:     NODE_LEN, NODE_RANGE, NODE_TYPE, NODE_APPEND, NODE_INPUT,
406:     NODE_SEQ,
407:     NODE_EXPR_STMT,
408:     NODE_FILE_READ, NODE_FILE_WRITE, NODE_FILE_APPEND,
409:     NODE_FSTRING,
410:     NODE_TRY_CATCH, NODE_THROW,
411:     NODE_LIST_COMP,
412:     NODE_LAMBDA,
413:     NODE_CALL_EXPR,
414:     NODE_INDEX_ASSIGN,
415:     NODE_IMPORT,
416:     NODE_IN,
417:     NODE_CLASS,
418:     NODE_TRAIT,
419:     NODE_USE_TRAIT,
420:     NODE_SPAWN,
421:     NODE_PACKED_ADD,
422:     NODE_PACKED_SUB,
423:     NODE_WHENEVER
424: } NodeType;
425: 
426: struct Node {
427:     NodeType type;
428:     Value value;
429:     const char *name;
430:     const char *second_name;
431:     Node *left, *right, *third;
432:     Node **elements; int element_count;
433:     Node **print_items; int print_count;
434:     Node **dict_keys, **dict_values; int dict_count;
435:     Node **call_args; int call_arg_count;
436:     char **func_params; int func_param_count;
437:     int uses_let;
438:     Node **fstring_parts; int fstring_part_count;
439:     Node *comp_expr;
440:     const char *comp_var;
441:     Node *comp_filter;
442:     struct Closure *closure;
443:     int line;
444:     const char *parent_name;
445:     Node **methods; int method_count;
446:     Node **fields; int field_count;
447: };
448: 
449: typedef Value (*NativeFuncPtr)(int argc, Value *args);
450: 
451: struct NativeFunc {
452:     GCObject obj;
453:     const char *name;
454:     NativeFuncPtr func;
455: };
456: 
457: typedef struct FuncEntry FuncEntry;
458: 
459: typedef uint64_t TypeSignature;
460: 
461: typedef struct {
462:     TypeSignature signature;
463:     void *jit_code;
464:     size_t jit_size;
465: } Specialization;
466: 
467: struct FuncEntry {
468:     const char *name;
469:     int param_count;
470:     char **params;
471:     Bytecode *body;
472:     int used;
473:     int uses_let;
474:     char **local_names;
475:     int local_count;
476:     int local_capacity;
477:     Specialization *specs;
478:     int spec_count;
479:     int spec_capacity;
480: };
481: 
482: struct BoundMethod {
483:     GCObject obj;
484:     Value instance;
485:     FuncEntry *method;
486: };
487: 
488: struct Trait {
489:     GCObject obj;
490:     const char *name;
491:     Dict *methods;
492: };
493: 
494: struct Shape {
495:     GCObject obj;
496:     struct Shape *parent;
497:     const char *name;
498:     int offset;
499:     Dict *transitions;
500: };
501: 
502: struct Class {
503:     GCObject obj;
504:     const char *name;
505:     struct Class *parent;
506:     Dict *methods;
507:     Shape *root_shape;
508:     Shape *stable_shape;
509:     int instances_created;
510: };
511: 
512: struct Instance {
513:     GCObject obj;
514:     struct Class *klass;
515:     Dict *properties;
516: };
517: 
518: struct List {
519:     GCObject obj;
520:     int refcount;
521:     int size;
522:     int capacity;
523:     Value *items;
524: };
525: 
526: struct Dict {
527:     GCObject obj;
528:     int refcount;
529:     int size;
530:     int capacity;
531:     Value *keys;
532:     Value *values;
533: };
534: 
535: struct Closure {
536:     GCObject obj;
537:     int refcount;
538:     int param_count;
539:     char **params;
540:     Bytecode *body;
541:     int captured_count;
542:     const char **captured_names;
543:     Value *captured_values;
544: };
545: 
546: extern int line_num;
547: extern Node *root;
548: extern FILE *yyin;
549: int yyparse(void);
550: void yyerror(const char *s);
551: Node *node_new(NodeType type);
552: void compile(Node *n);
553: double val_to_double(Value v);
554: 
555: /* Method Specialization Helpers */
556: TypeSignature compute_signature(Value *regs, int count);
557: void* func_get_specialization(FuncEntry *fe, TypeSignature sig);
558: void func_add_specialization(FuncEntry *fe, TypeSignature sig, void *code, size_t size);
559: 
560: typedef struct {
561:     int line;
562:     char *message;
563: } GlowError;
564: 
565: extern GlowError *glow_errors;
566: extern int glow_error_count;
567: void glow_clear_errors(void);
568: void glow_add_error(int line, const char *msg);
569: 
570: typedef struct yy_buffer_state *YY_BUFFER_STATE;
571: extern YY_BUFFER_STATE yy_scan_string(const char * str);
572: extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
573: 
574: #endif
575: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:11Z
Completed At: 2026-06-04T14:15:12Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/arena.h`
Total Lines: 12
Total Bytes: 228
Showing lines 1 to 12
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #ifndef GLOWLANG_ARENA_H
2: #define GLOWLANG_ARENA_H
3: 
4: #include <stddef.h>
5: 
6: void arena_init(void);
7: void *arena_alloc(size_t size);
8: void *arena_realloc(void *ptr, size_t old_size, size_t new_size);
9: void arena_free_all(void);
10: 
11: #endif
12: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:12Z
Completed At: 2026-06-04T14:15:13Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/arena.c`
Total Lines: 65
Total Bytes: 1680
Showing lines 1 to 65
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #include "arena.h"
2: #include <stdlib.h>
3: #include <string.h>
4: 
5: #define ARENA_CHUNK_SIZE (1024 * 1024) /* 1MB chunks */
6: 
7: typedef struct ArenaChunk {
8:     struct ArenaChunk *next;
9:     size_t used;
10:     char data[ARENA_CHUNK_SIZE];
11: } ArenaChunk;
12: 
13: static __thread ArenaChunk *arena_head = NULL;
14: 
15: void arena_init(void) {
16:     if (!arena_head) {
17:         arena_head = calloc(1, sizeof(ArenaChunk));
18:     }
19: }
20: 
21: void *arena_alloc(size_t size) {
22:     /* 8-byte alignment */
23:     size = (size + 7) & ~7;
24:     
25:     if (size > ARENA_CHUNK_SIZE) {
26:         /* Large allocations bypass the bump pointer and are not strictly managed by the block 
27:            in a real system, but for now we just allocate them raw. To avoid leaks in a pure arena, 
28:            we'd link them. For simplicity, we assume AST nodes and strings are small. */
29:         return calloc(1, size);
30:     }
31:     
32:     if (!arena_head) {
33:         arena_init();
34:     }
35:     
36:     if (arena_head->used + size > ARENA_CHUNK_SIZE) {
37:         ArenaChunk *new_chunk = calloc(1, sizeof(ArenaChunk));
38:         new_chunk->next = arena_head;
39:         arena_head = new_chunk;
40:     }
41:     
42:     void *ptr = arena_head->data + arena_head->used;
43:     arena_head->used += size;
44:     return ptr;
45: }
46: 
47: void *arena_realloc(void *ptr, size_t old_size, size_t new_size) {
48:     if (new_size == 0) return NULL;
49:     void *new_ptr = arena_alloc(new_size);
50:     if (ptr && old_size > 0) {
51:         memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
52:     }
53:     return new_ptr;
54: }
55: 
56: void arena_free_all(void) {
57:     ArenaChunk *curr = arena_head;
58:     while (curr) {
59:         ArenaChunk *next = curr->next;
60:         free(curr);
61:         curr = next;
62:     }
63:     arena_head = NULL;
64: }
65: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:13Z
Completed At: 2026-06-04T14:15:13Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/ir.h`
Total Lines: 77
Total Bytes: 1399
Showing lines 1 to 77
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #ifndef GLOWLANG_IR_H
2: #define GLOWLANG_IR_H
3: 
4: #include "glowlang.h"
5: 
6: typedef enum {
7:     IR_NOP,
8:     IR_CONST_INT,
9:     IR_CONST_FLOAT,
10:     IR_CONST_BOOL,
11:     IR_CONST_STRING,
12:     IR_LOAD_VAR,
13:     IR_STORE_VAR,
14:     IR_ADD,
15:     IR_SUB,
16:     IR_MUL,
17:     IR_DIV,
18:     IR_MOD,
19:     IR_LT,
20:     IR_GT,
21:     IR_LTE,
22:     IR_GTE,
23:     IR_EQ,
24:     IR_NEQ,
25:     IR_JUMP,
26:     IR_JUMP_IF_FALSE,
27:     IR_CALL,
28:     IR_RETURN,
29:     IR_PHI
30: } IROp;
31: 
32: typedef enum {
33:     TYPE_UNKNOWN = 0,
34:     TYPE_INT,
35:     TYPE_FLOAT,
36:     TYPE_BOOL,
37:     TYPE_STRING,
38:     TYPE_OBJECT
39: } IRType;
40: 
41: typedef struct IRInst {
42:     IROp op;
43:     int dst;   // SSA variable ID
44:     int src1;  // SSA variable ID
45:     int src2;  // SSA variable ID
46:     Value const_val;
47:     const char *name;
48:     IRType type;
49: } IRInst;
50: 
51: typedef struct IRBlock {
52:     int id;
53:     IRInst *insts;
54:     int count;
55:     int capacity;
56:     struct IRBlock *true_branch;
57:     struct IRBlock *false_branch;
58: } IRBlock;
59: 
60: typedef struct IRFunction {
61:     const char *name;
62:     IRBlock **blocks;
63:     int block_count;
64:     int block_capacity;
65:     int next_ssa_var;
66: } IRFunction;
67: 
68: IRFunction* ir_func_new(const char *name);
69: void ir_func_free(IRFunction *f);
70: IRBlock* ir_block_new(IRFunction *f);
71: int ir_emit(IRBlock *b, IROp op, int src1, int src2);
72: void ir_build_from_ast(IRFunction *f, struct Node *n);
73: void ir_optimize(IRFunction *f);
74: void ir_lower_to_bytecode(IRFunction *f, Bytecode *bc);
75: 
76: #endif
77: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:13Z
Completed At: 2026-06-04T14:15:14Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/ir.c`
Total Lines: 92
Total Bytes: 3182
Showing lines 1 to 92
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #include "ir.h"
2: #include "arena.h"
3: #include <stdlib.h>
4: #include <string.h>
5: #include <stdio.h>
6: 
7: IRFunction* ir_func_new(const char *name) {
8:     IRFunction *f = arena_alloc(sizeof(IRFunction));
9:     f->name = name;
10:     f->block_capacity = 8;
11:     f->blocks = arena_alloc(sizeof(IRBlock*) * f->block_capacity);
12:     f->block_count = 0;
13:     f->next_ssa_var = 1;
14:     return f;
15: }
16: 
17: IRBlock* ir_block_new(IRFunction *f) {
18:     IRBlock *b = arena_alloc(sizeof(IRBlock));
19:     b->id = f->block_count;
20:     b->capacity = 16;
21:     b->insts = arena_alloc(sizeof(IRInst) * b->capacity);
22:     b->count = 0;
23:     b->true_branch = NULL;
24:     b->false_branch = NULL;
25:     
26:     if (f->block_count >= f->block_capacity) {
27:         f->block_capacity *= 2;
28:         f->blocks = arena_realloc(f->blocks, sizeof(IRBlock*) * (f->block_capacity / 2), sizeof(IRBlock*) * f->block_capacity);
29:     }
30:     f->blocks[f->block_count++] = b;
31:     return b;
32: }
33: 
34: int ir_emit(IRBlock *b, IROp op, int src1, int src2) {
35:     if (b->count >= b->capacity) {
36:         b->capacity *= 2;
37:         b->insts = arena_realloc(b->insts, sizeof(IRInst) * (b->capacity / 2), sizeof(IRInst) * b->capacity);
38:     }
39:     int idx = b->count++;
40:     b->insts[idx].op = op;
41:     b->insts[idx].src1 = src1;
42:     b->insts[idx].src2 = src2;
43:     b->insts[idx].type = TYPE_UNKNOWN;
44:     return idx;
45: }
46: 
47: void ir_optimize(IRFunction *f) {
48:     // Pass 1: Constant Folding & Type Inference
49:     for (int i=0; i<f->block_count; i++) {
50:         IRBlock *b = f->blocks[i];
51:         for (int j=0; j<b->count; j++) {
52:             IRInst *inst = &b->insts[j];
53:             if (inst->op == IR_CONST_INT) {
54:                 inst->type = TYPE_INT;
55:             } else if (inst->op == IR_ADD) {
56:                 // If we know types of src1 and src2, we can infer type of ADD
57:                 // Find definitions
58:                 IRInst *def1 = NULL, *def2 = NULL;
59:                 for (int k=0; k<j; k++) {
60:                     if (b->insts[k].dst == inst->src1) def1 = &b->insts[k];
61:                     if (b->insts[k].dst == inst->src2) def2 = &b->insts[k];
62:                 }
63:                 if (def1 && def2 && def1->type == TYPE_INT && def2->type == TYPE_INT) {
64:                     inst->type = TYPE_INT;
65:                     if (def1->op == IR_CONST_INT && def2->op == IR_CONST_INT) {
66:                         // Fold!
67:                         inst->op = IR_CONST_INT;
68:                         inst->const_val = value_int(AS_INT(def1->const_val) + AS_INT(def2->const_val));
69:                     }
70:                 }
71:             }
72:         }
73:     }
74: }
75: 
76: // Convert SSA variable IDs to Register IDs
77: void ir_lower_to_bytecode(IRFunction *f, Bytecode *bc) {
78:     // For now, map each SSA variable exactly to a register to simulate basic SSA.
79:     // In a real backend, we'd do Graph Coloring Register Allocation.
80:     for (int i=0; i<f->block_count; i++) {
81:         IRBlock *b = f->blocks[i];
82:         for (int j=0; j<b->count; j++) {
83:             IRInst *inst = &b->insts[j];
84:             switch (inst->op) {
85:                 // Mapping IR -> Bytecode
86:                 // ... This is just a stub for now. The real generation happens in glowlang.y.
87:                 default: break;
88:             }
89:         }
90:     }
91: }
92: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:14Z
Completed At: 2026-06-04T14:15:15Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/opt.h`
Total Lines: 10
Total Bytes: 176
Showing lines 1 to 10
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #ifndef GLOWLANG_OPT_H
2: #define GLOWLANG_OPT_H
3: 
4: #include "glowlang.h"
5: 
6: // Optimize register-based bytecode using pseudo-SSA passes
7: void optimize_bytecode(Bytecode *bc);
8: 
9: #endif
10: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:15Z
Completed At: 2026-06-04T14:15:16Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/opt.c`
Total Lines: 260
Total Bytes: 10155
Showing lines 1 to 260
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #include "opt.h"
2: #include <stdlib.h>
3: #include <string.h>
4: #include <stdio.h>
5: 
6: void optimize_bytecode(Bytecode *bc) {
7:     if (!bc || bc->count == 0) return;
8: 
9:     // Pass 1: Identify all jump targets to define basic block boundaries
10:     signed char *is_jump_target = calloc(bc->count, sizeof(signed char));
11:     for (int i = 0; i < bc->count; i++) {
12:         Instruction *inst = &bc->code[i];
13:         if (inst->op == OP_JUMP || inst->op == OP_JUMP_IF_FALSE || inst->op == OP_JUMP_IF_TRUE) {
14:             if (inst->arg >= 0 && inst->arg < bc->count) {
15:                 is_jump_target[inst->arg] = 1;
16:             }
17:         }
18:     }
19: 
20:     Value const_vals[1024] = {0};
21:     signed char is_const[1024] = {0};
22:     unsigned char reg_types[1024] = {0}; // 0=Unknown, 1=Int, 2=Float, 3=String, 4=Bool
23: 
24:     // Pass 2: Type Inference and Constant Folding (Basic Block Local)
25:     for (int i=0; i < bc->count; i++) {
26:         Instruction *inst = &bc->code[i];
27: 
28:         // If this instruction is a jump target, it starts a new basic block.
29:         // We must invalidate all constant tracking because we don't know the state
30:         // coming from the jump source.
31:         if (is_jump_target[i]) {
32:             memset(is_const, 0, sizeof(is_const));
33:             memset(reg_types, 0, sizeof(reg_types));
34:         }
35: 
36:         if (inst->op == OP_CONST_INT) {
37:             if (inst->r0 < 1024) {
38:                 is_const[inst->r0] = 1;
39:                 const_vals[inst->r0] = value_int(inst->arg);
40:                 reg_types[inst->r0] = 1;
41:             }
42:             inst->type = 1;
43:         }
44:         else if (inst->op == OP_CONST_FLOAT) {
45:             if (inst->r0 < 1024) {
46:                 reg_types[inst->r0] = 2;
47:             }
48:             inst->type = 2;
49:         }
50:         else if (inst->op == OP_CONST_STRING) {
51:             if (inst->r0 < 1024) {
52:                 reg_types[inst->r0] = 3;
53:             }
54:             inst->type = 3;
55:         }
56:         else if (inst->op == OP_CONST_BOOL) {
57:             if (inst->r0 < 1024) {
58:                 is_const[inst->r0] = 1;
59:                 const_vals[inst->r0] = value_bool(inst->arg);
60:                 reg_types[inst->r0] = 4;
61:             }
62:             inst->type = 4;
63:         }
64:         else if (inst->op == OP_ADD || inst->op == OP_SUB || inst->op == OP_MUL || inst->op == OP_DIV) {
65:             int r0 = inst->r0;
66:             int r1 = inst->r1;
67:             int r2 = inst->r2;
68:             
69:             if (r1 < 1024 && r2 < 1024) {
70:                 // Type Inference
71:                 if (reg_types[r1] == 1 && reg_types[r2] == 1) {
72:                     if (r0 < 1024) reg_types[r0] = 1;
73:                     inst->type = 1;
74:                 } else if (reg_types[r1] == 2 || reg_types[r2] == 2) {
75:                     if (r0 < 1024) reg_types[r0] = 2;
76:                     inst->type = 2;
77:                 }
78:                 
79:                 // Constant folding
80:                 if (is_const[r1] && is_const[r2]) {
81:                     Value v1 = const_vals[r1];
82:                     Value v2 = const_vals[r2];
83:                     if (GET_TYPE(v1) == VAL_INT && GET_TYPE(v2) == VAL_INT) {
84:                         int64_t i1 = AS_INT(v1);
85:                         int64_t i2 = AS_INT(v2);
86:                         int64_t res = 0;
87:                         int ok = 1;
88:                         if (inst->op == OP_ADD) res = i1 + i2;
89:                         else if (inst->op == OP_SUB) res = i1 - i2;
90:                         else if (inst->op == OP_MUL) res = i1 * i2;
91:                         else if (inst->op == OP_DIV) { if (i2 != 0) res = i1 / i2; else ok = 0; }
92:                         
93:                         if (ok) {
94:                             inst->op = OP_CONST_INT;
95:                             inst->arg = (int)res;
96:                             if (r0 < 1024) {
97:                                 is_const[r0] = 1;
98:                                 const_vals[r0] = value_int(res);
99:                                 reg_types[r0] = 1;
100:                             }
101:                             inst->type = 1;
102:                         }
103:                     }
104:                 }
105:             }
106:         }
107:         else if (inst->op == OP_MOVE) {
108:             if (inst->r1 < 1024 && inst->r0 < 1024) {
109:                 is_const[inst->r0] = is_const[inst->r1];
110:                 const_vals[inst->r0] = const_vals[inst->r1];
111:                 reg_types[inst->r0] = reg_types[inst->r1];
112:                 inst->type = reg_types[inst->r1];
113:             }
114:         } else {
115:             if (inst->r0 < 1024) {
116:                 is_const[inst->r0] = 0;
117:             }
118:         }
119:     }
120: 
121:     // Pass 3: Scalar Replacement
122:     int virtual_bases[1024];
123:     int virtual_sizes[1024];
124:     for (int j=0; j<1024; j++) { virtual_bases[j] = -1; virtual_sizes[j] = 0; }
125: 
126:     for (int i=0; i < bc->count; i++) {
127:         Instruction *inst = &bc->code[i];
128: 
129:         if (is_jump_target[i]) {
130:             for (int j=0; j<1024; j++) { virtual_bases[j] = -1; virtual_sizes[j] = 0; }
131:         }
132: 
133:         if (inst->op == OP_LIST_LITERAL && inst->is_stack_alloc && inst->arg <= 4) {
134:             if (inst->r0 < 1024) {
135:                 virtual_bases[inst->r0] = inst->r1;
136:                 virtual_sizes[inst->r0] = inst->arg;
137:                 inst->op = OP_NOP;
138:             }
139:         }
140:         else if (inst->op == OP_INDEX) {
141:             int coll_reg = inst->r1;
142:             int idx_reg = inst->r2;
143:             if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
144:                 if (idx_reg < 1024 && is_const[idx_reg]) {
145:                     int k = (int)AS_INT(const_vals[idx_reg]);
146:                     if (k >= 0 && k < virtual_sizes[coll_reg]) {
147:                         inst->op = OP_MOVE;
148:                         inst->r1 = virtual_bases[coll_reg] + k;
149:                     }
150:                 }
151:             }
152:         }
153:         else if (inst->op == OP_LEN) {
154:              int coll_reg = inst->r1;
155:              if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
156:                  inst->op = OP_CONST_INT;
157:                  inst->arg = virtual_sizes[coll_reg];
158:              }
159:         }
160:         else if (inst->op == OP_STORE_INDEX) {
161:             int coll_reg = inst->r0;
162:             int idx_reg = inst->r1;
163:             int src_reg = inst->r2;
164:             if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
165:                 if (idx_reg < 1024 && is_const[idx_reg]) {
166:                     int k = (int)AS_INT(const_vals[idx_reg]);
167:                     if (k >= 0 && k < virtual_sizes[coll_reg]) {
168:                         inst->op = OP_MOVE;
169:                         inst->r0 = virtual_bases[coll_reg] + k;
170:                         inst->r1 = src_reg;
171:                     }
172:                 }
173:             }
174:         }
175:     }
176:     
177:     // Pass 4: Loop Auto-Vectorization
178:     for (int i = 0; i < bc->count - 12; i++) {
179:         Instruction *lt = &bc->code[i];
180:         if (lt->op == OP_LTE || lt->op == OP_LT) {
181:             Instruction *jf = &bc->code[i+1];
182:             if (jf->op == OP_JUMP_IF_FALSE) {
183:                 int exit_pc = jf->arg;
184:                 // Pattern for res[i] = a[i] + b[i]
185:                 // 4: LOAD_VAR (i) -> r_idx
186:                 // 34: INDEX (a, r_idx) -> r_a
187:                 // 4: LOAD_VAR (i) -> r_idx2
188:                 // 34: INDEX (b, r_idx2) -> r_b
189:                 // 6: ADD (r_a, r_b) -> r_res
190:                 // 36: STORE_INDEX (res, r_idx, r_res)
191:                 
192:                 if (bc->code[i+2].op == OP_LOAD_VAR && 
193:                     bc->code[i+3].op == OP_INDEX &&
194:                     bc->code[i+4].op == OP_LOAD_VAR &&
195:                     bc->code[i+5].op == OP_INDEX &&
196:                     bc->code[i+6].op == OP_ADD &&
197:                     bc->code[i+7].op == OP_STORE_INDEX) {
198:                     
199:                     // Match!
200:                     jf->op = OP_SIMD_ADD;
201:                     jf->r0 = bc->code[i+7].r0; // target collection
202:                     jf->r1 = bc->code[i+3].r1; // a collection
203:                     jf->r2 = bc->code[i+5].r1; // b collection
204:                     
205:                     // Cleanup loop body
206:                     for (int k = i+2; k < exit_pc && k < bc->count - 1; k++) {
207:                         if (bc->code[k].op != OP_JUMP) bc->code[k].op = OP_NOP;
208:                     }
209:                 }
210:             }
211:         }
212:     }
213:     // Pass 4.5: Devirtualize CALL_VALUE
214:     for (int i = 0; i < bc->count - 1; i++) {
215:         Instruction *load = &bc->code[i];
216:         Instruction *call = &bc->code[i+1];
217:         if (load->op == OP_LOAD_VAR && call->op == OP_CALL_VALUE && load->r0 == call->r1) {
218:             const char *name = global_table.names[load->arg];
219:             call->op = OP_CALL;
220:             call->ptr = (intptr_t)name;
221:             load->op = OP_NOP;
222:         }
223:     }
224:     
225:     // Pass 5: Tail Call & Self-Call Fast Optimization (Early Decision)
226:     for (int i = 0; i < bc->count; i++) {
227:         Instruction *pc = &bc->code[i];
228: 
229:         if (pc->op == OP_CALL || pc->op == OP_CALL_VALUE || pc->op == OP_CALL_SELF_FAST) {
230:             // Look ahead for RETURN, skipping NOPs
231:             int next_idx = i + 1;
232:             while (next_idx < bc->count && bc->code[next_idx].op == OP_NOP) next_idx++;
233:             
234:             if (next_idx < bc->count && bc->code[next_idx].op == OP_RETURN && pc->r0 == bc->code[next_idx].r0) {
235:                 pc->op = OP_TAIL_CALL;
236:                 bc->code[next_idx].op = OP_NOP;
237:                 continue;
238:             }
239:         }
240: 
241:         // 2. Self-Call Fast Path (if not already TCO'd)
242:         if (pc->op == OP_CALL) {
243:              const char *name = (const char*)pc->ptr;
244:              if (name && bc->owner && bc->owner->name && strcmp(name, bc->owner->name) == 0) {
245:                  pc->op = OP_CALL_SELF_FAST;
246:                  
247:                  // Re-check for TCO after patching to SELF_FAST
248:                  int next_idx = i + 1;
249:                  while (next_idx < bc->count && bc->code[next_idx].op == OP_NOP) next_idx++;
250:                  if (next_idx < bc->count && bc->code[next_idx].op == OP_RETURN && pc->r0 == bc->code[next_idx].r0) {
251:                      pc->op = OP_TAIL_CALL;
252:                      bc->code[next_idx].op = OP_NOP;
253:                  }
254:              }
255:         }
256:     }
257: 
258:     free(is_jump_target);
259: }
260: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:16Z
Completed At: 2026-06-04T14:15:16Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/jit.h`
Total Lines: 12
Total Bytes: 278
Showing lines 1 to 12
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #ifndef GLOWLANG_JIT_H
2: #define GLOWLANG_JIT_H
3: 
4: #include "glowlang.h"
5: 
6: void jit_compile(Bytecode *bc);
7: void jit_compile_trace(Trace *t);
8: void jit_compile_specialized(FuncEntry *fe, TypeSignature sig);
9: void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature sig);
10: 
11: #endif
12: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:16Z
Completed At: 2026-06-04T14:15:17Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/jit.c`
Total Lines: 433
Total Bytes: 30654
Showing lines 1 to 433
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #include "jit.h"
2: #include <sys/mman.h>
3: #include <string.h>
4: #include <stdio.h>
5: #include <stdlib.h>
6: 
7: extern FuncEntry* func_get(const char *name);
8: 
9: typedef struct { uint8_t *code; size_t size; size_t capacity; } JITBuffer;
10: 
11: static void jit_emit_byte(JITBuffer *buf, uint8_t b) { 
12:     if (buf->size < buf->capacity) buf->code[buf->size++] = b; 
13: }
14: static void jit_emit_u32(JITBuffer *buf, uint32_t v) { 
15:     if (buf->size + 4 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 4); buf->size += 4; } 
16: }
17: static void jit_emit_u64(JITBuffer *buf, uint64_t v) { 
18:     if (buf->size + 8 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 8); buf->size += 8; } 
19: }
20: 
21: #if defined(__x86_64__) || defined(_M_X64)
22: static void emit_mov_rax_imm64(JITBuffer *buf, uint64_t imm) { 
23:     jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xb8); jit_emit_u64(buf, imm); 
24: }
25: 
26: static void emit_mov_rcx_imm64(JITBuffer *buf, uint64_t imm) { 
27:     jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xb9); jit_emit_u64(buf, imm); 
28: }
29: 
30: static void emit_mov_rdx_imm64(JITBuffer *buf, uint64_t imm) { 
31:     jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, imm); 
32: }
33: 
34: static void emit_mov_rdi_imm64(JITBuffer *buf, uint64_t imm) { 
35:     jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xbf); jit_emit_u64(buf, imm); 
36: }
37: 
38: static void emit_mov_rsi_imm64(JITBuffer *buf, uint64_t imm) { 
39:     jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xbe); jit_emit_u64(buf, imm); 
40: }
41: 
42: static void emit_mov_r10_imm64(JITBuffer *buf, uint64_t imm) { 
43:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, imm); 
44: }
45: 
46: static void emit_mov_r11_imm64(JITBuffer *buf, uint64_t imm) { 
47:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xbb); jit_emit_u64(buf, imm); 
48: }
49: 
50: static void emit_mov_rax_reg(JITBuffer *buf, int reg) {
51:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
52: }
53: static void emit_mov_reg_rax(JITBuffer *buf, int reg) {
54:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
55: }
56: static void emit_mov_rcx_reg(JITBuffer *buf, int reg) {
57:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8c); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
58: }
59: static void emit_mov_rdx_reg(JITBuffer *buf, int reg) {
60:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x94); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
61: }
62: static void emit_mov_rdi_reg(JITBuffer *buf, int reg) {
63:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xbc); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
64: }
65: static void emit_mov_rsi_reg(JITBuffer *buf, int reg) {
66:     jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xb4); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
67: }
68: 
69: void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
70:     (void)current_sig;
71:     if (bc->jit_code || bc->jit_failed) return;
72: 
73:     for (int i = 0; i < bc->count; i++) {
74:         Instruction *pc = &bc->code[i];
75:         switch (pc->op) {
76:             case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
77:             case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
78:             case OP_MOVE: case OP_NOP:
79:             case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
80:             case OP_GET_PROPERTY: case OP_LT: case OP_GT: case OP_LTE:
81:             case OP_GTE: case OP_EQ: case OP_NEQ: case OP_JUMP:
82:             case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
83:             case OP_CALL: case OP_CALL_VALUE:
84:             case OP_INDEX: case OP_STORE_INDEX:
85:             case OP_SIMD_ADD: case OP_TAIL_CALL:
86:                 break;
87:             default: bc->jit_failed = 1; return;
88:         }
89:     }
90: 
91:     size_t size = 65536; 
92:     uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
93:     if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
94:     JITBuffer buf = {code, 0, size};
95: 
96:     // Prologue
97:     jit_emit_byte(&buf, 0x55); 
98:     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5);
99:     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x54); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x55);
100:     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x56); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x57);
101:     jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xfc); // r12 = R
102:     jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xf5); // r13 = pc_idx
103: 
104:     uint32_t body_start = buf.size;
105: 
106:     uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
107:     struct { int instr_idx; uint32_t patch_pos; } *jumps = calloc(bc->count + 1, sizeof(*jumps));
108:     int jump_count = 0;
109: 
110:     for (int i = 0; i < bc->count; i++) {
111:         offsets[i] = buf.size;
112:         Instruction *pc = &bc->code[i];
113:         Instruction *orig_pc = (fe && fe->body) ? &fe->body->code[i] : pc;
114:         switch (pc->op) {
115:             case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
116:             case OP_CONST_FLOAT: emit_mov_rax_imm64(&buf, *(uint64_t*)pc->ptr); emit_mov_reg_rax(&buf, pc->r0); break;
117:             case OP_CONST_BOOL: emit_mov_rax_imm64(&buf, value_bool(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
118:             case OP_LOAD_VAR: {
119:                 emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
120:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
121:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x81);
122:                 jit_emit_u32(&buf, pc->arg * 8);
123:                 emit_mov_reg_rax(&buf, pc->r0);
124:                 break;
125:             }
126:             case OP_STORE_VAR:
127:             case OP_STORE_LET: {
128:                 emit_mov_rax_reg(&buf, pc->r0);
129:                 emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
130:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
131:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x81);
132:                 jit_emit_u32(&buf, pc->arg * 8);
133:                 break;
134:             }
135:             case OP_MOVE: emit_mov_rax_reg(&buf, pc->r1); emit_mov_reg_rax(&buf, pc->r0); break;
136:             case OP_NOP: break;
137:             case OP_ADD:
138:             case OP_SUB:
139:             case OP_MUL:
140:             case OP_DIV: {
141:                 if (pc->type == 1 && (pc->op == OP_ADD || pc->op == OP_SUB)) {
142:                     emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
143:                     emit_mov_r10_imm64(&buf, 0xFFFF000000000000ULL);
144:                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc6);
145:                     jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
146:                     emit_mov_r11_imm64(&buf, 0x7FF8000000000000ULL);
147:                     jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
148:                     uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
149:                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xce);
150:                     jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
151:                     jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
152:                     uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
153:                     if (pc->op == OP_ADD) { 
154:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8);
155:                         jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xd8);
156:                     } else if (pc->op == OP_SUB) { 
157:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8);
158:                         jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd8);
159:                     }
160:                     uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
161:                     uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
162:                     emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00);
163:                     emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
164:                     uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
165:                     emit_mov_reg_rax(&buf, pc->r0);
166:                 } else if (pc->type == 2) {
167:                     emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
168:                     jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc0);
169:                     jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc1);
170:                     if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x58); jit_emit_byte(&buf, 0xc1); }
171:                     else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0xc1); }
172:                     else if (pc->op == OP_MUL) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x59); jit_emit_byte(&buf, 0xc1); }
173:                     else if (pc->op == OP_DIV) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0xc1); }
174:                     else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
175:                     jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x7e); jit_emit_byte(&buf, 0xc0);
176:                     emit_mov_reg_rax(&buf, pc->r0);
177:                 } else {
178:                     emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
179:                     if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
180:                     else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
181:                     else if (pc->op == OP_MUL) {
182:                         emit_mov_rdx_imm64(&buf, 0x0000FFFFFFFFFFFFULL);
183:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd0);
184:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd1);
185:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc1);
186:                         emit_mov_rdx_imm64(&buf, 0x7FF8000000000000ULL);
187:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd0);
188:                     } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
189:                     emit_mov_reg_rax(&buf, pc->r0);
190:                 }
191:                 break;
192:             }
193:             case OP_GET_PROPERTY: {
194:                 if (pc->cache[0].key) {
195:                     emit_mov_rax_reg(&buf, pc->r1);
196:                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0xd0); emit_mov_rsi_imm64(&buf, MASK_NAN | (6ULL << 48)); emit_mov_rdi_imm64(&buf, MASK_NAN | (7ULL << 48)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xfa); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xf2);
197:                     uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
198:                     emit_mov_rsi_imm64(&buf, 0x0000FFFFFFFFFFFFULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xf0); emit_mov_rsi_imm64(&buf, (uintptr_t)pc->cache[0].key); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x70); jit_emit_byte(&buf, 40);
199:                     uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
200:                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, 48); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, pc->cache[0].val * 8);
201:                     uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
202:                     uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
203:                     emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00); emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
204:                     uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
205:                     emit_mov_reg_rax(&buf, pc->r0);
206:                 } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
207:                 break;
208:             }
209:             case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
210:                 emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
211:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8);
212:                 uint8_t set_op;
213:                 switch(pc->op) { case OP_LT: set_op = 0x9c; break; case OP_GT: set_op = 0x9f; break; case OP_LTE: set_op = 0x9e; break; case OP_GTE: set_op = 0x9d; break; case OP_EQ: set_op = 0x94; break; case OP_NEQ: set_op = 0x95; break; default: set_op = 0x94; break; }
214:                 jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, set_op); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xb6); jit_emit_byte(&buf, 0xc1); emit_mov_rax_imm64(&buf, value_bool(0)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); emit_mov_reg_rax(&buf, pc->r0);
215:                 break;
216:             }
217:             case OP_JUMP: { jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
218:             case OP_JUMP_IF_FALSE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
219:             case OP_JUMP_IF_TRUE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
220:             case OP_CALL:
221:             case OP_CALL_VALUE: {
222:                 FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
223:                 jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
224:                 jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
225:                 jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
226:                 emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
227:                 emit_mov_rax_imm64(&buf, (uintptr_t)compute_signature);
228:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
229:                 jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
230:                 jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
231:                 emit_mov_rcx_imm64(&buf, (uintptr_t)&orig_pc->cache[0]);
232:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x01);
233:                 uint32_t jne_cache = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
234:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0x79); jit_emit_byte(&buf, 0x08); jit_emit_byte(&buf, 0x00);
235:                 uint32_t je_null = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jit_emit_u32(&buf, 0);
236:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x08);
237:                 jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
238:                 jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xee);
239:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
240:                 uint32_t jmp_done = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
241:                 uint32_t slow_path = buf.size; 
242:                 *(uint32_t*)(buf.code + jne_cache + 2) = (uint32_t)(slow_path - (jne_cache + 6));
243:                 *(uint32_t*)(buf.code + je_null + 2) = (uint32_t)(slow_path - (je_null + 6));
244:                 emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
245:                 jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2);
246:                 emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
247:                 if (target_fe) { 
248:                     emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
249:                     emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
250:                 } else { 
251:                     emit_mov_rax_reg(&buf, pc->r1); 
252:                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
253:                     emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
254:                 }
255:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
256:                 uint32_t done = buf.size; *(uint32_t*)(buf.code + jmp_done + 1) = (uint32_t)(done - (jmp_done + 5));
257:                 emit_mov_reg_rax(&buf, pc->r0);
258:                 break;
259:             }
260:             case OP_TAIL_CALL: {
261:                 FuncEntry *target_fe = (pc->ptr) ? func_get((const char*)pc->ptr) : NULL;
262:                 if (target_fe && target_fe == fe) {
263:                     for (int k = 0; k < pc->arg && k < fe->param_count; k++) {
264:                         emit_mov_rax_reg(&buf, pc->r2 + k);
265:                         emit_mov_reg_rax(&buf, k);
266:                     }
267:                     jit_emit_byte(&buf, 0xe9);
268:                     jumps[jump_count].instr_idx = -1; // Special index for body_start
269:                     jumps[jump_count].patch_pos = buf.size;
270:                     jit_emit_u32(&buf, 0);
271:                     jump_count++;
272:                 } else {
273:                     // Dynamic or cross-function tail call
274:                     // 1. Prepare args
275:                     jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // r10 = R
276:                     jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // r10 points to args
277:                     
278:                     // 2. Setup call info
279:                     emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
280:                     jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2); // rdx = r10 (args)
281:                     emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg); // rsi = argc
282: 
283:                     if (target_fe) { 
284:                         emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
285:                         emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
286:                     } else { 
287:                         emit_mov_rax_reg(&buf, pc->r1); // Load callee
288:                         jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // rdi = callee
289:                         emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
290:                     }
291:                     
292:                     // 3. Epilogue (Pop frame) before jumping
293:                     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); 
294:                     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); 
295:                     jit_emit_byte(&buf, 0x5d);
296:                     
297:                     // 4. Jump (true tail call since we popped our frame)
298:                     jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0); // jmp rax
299:                 }
300:                 break;
301:             }
302:             case OP_INDEX: {
303:                 emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2);
304:                 emit_mov_rax_imm64(&buf, (uintptr_t)packed_array_get_jit);
305:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
306:                 emit_mov_reg_rax(&buf, pc->r0);
307:                 break;
308:             }
309:             case OP_STORE_INDEX: {
310:                 emit_mov_rdi_reg(&buf, pc->r0); emit_mov_rsi_reg(&buf, pc->r1); emit_mov_rdx_reg(&buf, pc->r2);
311:                 emit_mov_rax_imm64(&buf, (uintptr_t)packed_array_set_jit);
312:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
313:                 break;
314:             }
315:             case OP_SIMD_ADD: {
316:                 emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2); emit_mov_rdx_reg(&buf, pc->r0);
317:                 emit_mov_rax_imm64(&buf, (uintptr_t)simd_add_helper);
318:                 jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
319:                 jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
320:                 break;
321:             }
322:             case OP_RETURN: { 
323:                 emit_mov_rax_reg(&buf, pc->r0); 
324:                 jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3); 
325:                 break; 
326:             }
327:             default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
328:         }
329:     }
330:     offsets[bc->count] = buf.size;
331:     for (int i = 0; i < jump_count; i++) { 
332:         uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx]; 
333:         uint32_t patch_pos = jumps[i].patch_pos; 
334:         int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4); 
335:         memcpy(code + patch_pos, &rel, 4); 
336:     }
337:     mprotect(code, size, PROT_READ | PROT_EXEC);
338:     bc->jit_code = code; bc->jit_size = buf.size;
339:     free(offsets); free(jumps);
340: }
341: 
342: void jit_compile(Bytecode *bc) { jit_compile_ext(bc, NULL, 0); }
343: 
344: static void propagate_types(Instruction *code, int count, int param_count, TypeSignature sig, FuncEntry *fe) {
345:     unsigned char reg_types[256]; memset(reg_types, 0, 256);
346:     for (int i = 0; i < param_count && i < 16; i++) reg_types[i] = (sig >> (i * 4)) & 0xF;
347:     for (int i = 0; i < count; i++) {
348:         Instruction *pc = &code[i];
349:         switch (pc->op) {
350:             case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
351:                 if (reg_types[pc->r1] == 1 && reg_types[pc->r2] == 1) { pc->type = 1; reg_types[pc->r0] = 1; }
352:                 else if ((reg_types[pc->r1] == 1 || reg_types[pc->r1] == 2) && (reg_types[pc->r2] == 1 || reg_types[pc->r2] == 2)) { pc->type = 2; reg_types[pc->r0] = 2; }
353:                 break;
354:             case OP_CONST_INT: reg_types[pc->r0] = 1; pc->type = 1; break;
355:             case OP_CONST_FLOAT: reg_types[pc->r0] = 2; pc->type = 2; break;
356:             case OP_MOVE: reg_types[pc->r0] = reg_types[pc->r1]; break;
357:             case OP_CALL: {
358:                 FuncEntry *target_fe = func_get((const char*)pc->ptr);
359:                 if (target_fe && target_fe == fe && sig == 1) reg_types[pc->r0] = 1;
360:                 else reg_types[pc->r0] = 0;
361:                 break;
362:             }
363:             default: break;
364:         }
365:     }
366: }
367: 
368: void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) {
369:     if (!fe || !fe->body) return;
370:     Bytecode *sbc = calloc(1, sizeof(Bytecode));
371:     sbc->count = fe->body->count;
372:     sbc->code = malloc(sizeof(Instruction) * sbc->count);
373:     memcpy(sbc->code, fe->body->code, sizeof(Instruction) * sbc->count);
374:     for (int i=0; i<sbc->count; i++) sbc->code[i].trace = NULL;
375:     sbc->local_count = fe->body->local_count;
376:     propagate_types(sbc->code, sbc->count, fe->param_count, sig, fe);
377:     jit_compile_ext(sbc, fe, sig);
378:     if (sbc->jit_code) func_add_specialization(fe, sig, sbc->jit_code, sbc->jit_size);
379:     else func_add_specialization(fe, sig, (void*)1, 0);
380:     free(sbc->code); free(sbc);
381: }
382: 
383: void jit_compile_trace(Trace *t) {
384:     if (!t || t->jit_code) return;
385:     size_t size = 16384;
386:     uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
387:     if (code == MAP_FAILED) return;
388:     JITBuffer buf = {code, 0, size};
389:     jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5);
390:     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x54); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x55);
391:     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x56); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x57);
392:     jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xfc); jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xf5);
393:     for (int i = 0; i < t->count; i++) {
394:         Instruction *pc = &t->instructions[i];
395:         switch (pc->op) {
396:             case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
397:             case OP_ADD:
398:             case OP_SUB: {
399:                 emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
400:                 if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
401:                 else { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
402:                 emit_mov_reg_rax(&buf, pc->r0);
403:                 break;
404:             }
405:             case OP_JUMP_IF_FALSE: {
406:                 emit_mov_rax_reg(&buf, pc->r0);
407:                 jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0);
408:                 uint32_t patch = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jit_emit_u32(&buf, 0);
409:                 uint32_t exit_pos = buf.size; *(uint32_t*)(buf.code + patch + 2) = (uint32_t)(exit_pos - (patch + 6));
410:                 emit_mov_rax_imm64(&buf, (uint64_t)t->original_offsets[i]); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00); emit_mov_rax_imm64(&buf, MASK_NAN | (16ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
411:                 break;
412:             }
413:             case OP_JUMP: { if (pc->arg == (int)(t->start_pc - t->instructions)) { emit_mov_rax_imm64(&buf, 0); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3); } break; }
414:             default: break;
415:         }
416:     }
417:     jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
418:     mprotect(code, size, PROT_READ | PROT_EXEC);
419:     t->jit_code = code; t->jit_size = size;
420: }
421: 
422: #elif defined(__aarch64__) || defined(_M_ARM64)
423: void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
424: void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
425: void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }
426: void jit_compile_trace(Trace *t) { if (t) t->jit_code = NULL; }
427: #else
428: void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
429: void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
430: void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }
431: void jit_compile_trace(Trace *t) { if (t) t->jit_code = NULL; }
432: #endif
433: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Created At: 2026-06-04T14:15:17Z
Completed At: 2026-06-04T14:15:17Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/run_bytecode_optimized.c`
Total Lines: 687
Total Bytes: 36641
Showing lines 1 to 687
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: static void print_value(Value v) {
2:     if (GET_TYPE(v) == VAL_INT)    printf("%d", AS_INT(v));
3:     else if (GET_TYPE(v) == VAL_FLOAT)  printf("%g", AS_FLOAT(v));
4:     else if (GET_TYPE(v) == VAL_BOOL)   printf("%s", AS_BOOL(v) ? "true" : "false");
5:     else if (GET_TYPE(v) == VAL_STRING) printf("%s", AS_STRING(v));
6:     else if (GET_TYPE(v) == VAL_LIST) {
7:         printf("[");
8:         for (int j = 0; j < AS_LIST(v)->size; j++) {
9:             print_value(AS_LIST(v)->items[j]);
10:             if (j < AS_LIST(v)->size - 1) printf(", ");
11:         }
12:         printf("]");
13:     }
14:     else if (GET_TYPE(v) == VAL_DICT) {
15:         printf("{");
16:         for (int j = 0; j < AS_DICT(v)->size; j++) {
17:             printf("\"%s\": ", AS_DICT(v)->keys[j]);
18:             print_value(AS_DICT(v)->values[j]);
19:             if (j < AS_DICT(v)->size - 1) printf(", ");
20:         }
21:         printf("}");
22:     }
23: }
24: 
25: static Value run_bytecode(Bytecode *bc, int base_offset) {
26:     static void* dispatch_table[] = {
27:         &&label_OP_CONST_INT, &&label_OP_CONST_FLOAT, &&label_OP_CONST_STRING, &&label_OP_CONST_BOOL,
28:         &&label_OP_LOAD_VAR, &&label_OP_STORE_VAR,
29:         &&label_OP_ADD, &&label_OP_SUB, &&label_OP_MUL, &&label_OP_DIV, &&label_OP_MOD, &&label_OP_POW, &&label_OP_NEG,
30:         &&label_OP_LT, &&label_OP_GT, &&label_OP_LTE, &&label_OP_GTE, &&label_OP_EQ, &&label_OP_NEQ,
31:         &&label_OP_AND, &&label_OP_OR, &&label_OP_NOT,
32:         &&label_OP_STRING_CONCAT,
33:         &&label_OP_JUMP, &&label_OP_JUMP_IF_FALSE, &&label_OP_JUMP_IF_TRUE,
34:         &&label_OP_CALL_BUILTIN_LEN, &&label_OP_CALL_BUILTIN_RANGE, &&label_OP_CALL_BUILTIN_TYPE,
35:         &&label_OP_BUILTIN_APPEND, &&label_OP_BUILTIN_INPUT, &&label_OP_BUILTIN_SHOW, &&label_OP_BUILTIN_WRITE,
36:         &&label_OP_LIST_LITERAL, &&label_OP_DICT_LITERAL,
37:         &&label_OP_INDEX, &&label_OP_SLICE, &&label_OP_STORE_INDEX,
38:         &&label_OP_FUNC_DEF, &&label_OP_CALL, &&label_OP_RETURN,
39:         &&label_OP_FOR_EACH, &&label_OP_FOR_EACH_PAIR,
40:         &&label_OP_STOP, &&label_OP_SKIP,
41:         &&label_OP_POP,
42:         &&label_OP_PUSH_SCOPE, &&label_OP_POP_SCOPE, &&label_OP_STORE_LET,
43:         &&label_OP_LOAD_LOCAL, &&label_OP_STORE_LOCAL,
44:         &&label_OP_DICT_KEYS, &&label_OP_DICT_VALUES, &&label_OP_DICT_HAS, &&label_OP_DICT_REMOVE, &&label_OP_DICT_MERGE,
45:         &&label_OP_MATH_ABS, &&label_OP_MATH_MIN, &&label_OP_MATH_MAX, &&label_OP_MATH_ROUND, &&label_OP_MATH_FLOOR, &&label_OP_MATH_CEIL, &&label_OP_MATH_SQRT,
46:         &&label_OP_STR_UPPER, &&label_OP_STR_LOWER, &&label_OP_STR_TRIM, &&label_OP_STR_FIND, &&label_OP_STR_REPLACE, &&label_OP_STR_STARTSWITH, &&label_OP_STR_ENDSWITH, &&label_OP_STR_SPLIT,
47:         &&label_OP_LIST_PUSH, &&label_OP_LIST_POP, &&label_OP_LIST_INSERT, &&label_OP_LIST_REMOVE, &&label_OP_LIST_CONTAINS, &&label_OP_LIST_SORT, &&label_OP_LIST_REVERSE,
48:         &&label_OP_FILE_READ, &&label_OP_FILE_WRITE, &&label_OP_FILE_APPEND,
49:         &&label_OP_FSTRING,
50:         &&label_OP_LIST_COMP,
51:         &&label_OP_THROW, &&label_OP_TRY_START, &&label_OP_TRY_END,
52:         &&label_OP_LAMBDA,
53:         &&label_OP_CALL_VALUE
54:     };
55: 
56:     int ip = 0;
57:     int base_sp = sp;
58:     vm_loop_stop = 0; vm_loop_skip = 0; vm_has_return = 0;
59: 
60:     #define DISPATCH() \
61:         do { \
62:             if (ip >= bc->count) goto label_EXIT_VM; \
63:             if (vm_exception_active) goto label_HANDLE_EXCEPTION; \
64:             goto *dispatch_table[bc->code[ip].op]; \
65:         } while (0)
66: 
67:     DISPATCH();
68: 
69: label_HANDLE_EXCEPTION:
70:     if (hsp > 0) {
71:         if (handler_stack[hsp-1].csp == csp) {
72:             TryHandler h = handler_stack[--hsp];
73:             vm_exception_active = 0;
74:             ip = h.catch_ip; sp = h.sp;
75:             var_restore_shadow(h.shadow_list);
76:             PUSH(vm_exception_value);
77:             DISPATCH();
78:         } else {
79:             if (csp > 0) {
80:                 csp--; CallFrame *cf = &call_stack[csp];
81:                 if ((cf->pending_fn && cf->pending_fn->uses_let) || cf->pending_closure) var_restore_shadow(cf->old_shadow_list);
82:                 if (cf->param_count > 0) var_set(cf->p0, cf->v0);
83:                 if (cf->param_count > 1) var_set(cf->p1, cf->v1);
84:                 if (cf->local_values && cf->local_count > 16) free(cf->local_values);
85:                 bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
86:                 vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
87:                 DISPATCH();
88:             } else {
89:                 printf("Unhandled exception: ");
90:                 if (GET_TYPE(vm_exception_value) == VAL_STRING) printf("%s\n", AS_STRING(vm_exception_value));
91:                 else printf("type %s\n", type_name(GET_TYPE(vm_exception_value)));
92:                 exit(1);
93:             }
94:         }
95:     } else {
96:         printf("Unhandled exception at top level\n");
97:         exit(1);
98:     }
99: 
100: label_OP_CONST_INT:
101:     PUSH(value_int(bc->code[ip].arg)); ip++; DISPATCH();
102: 
103: label_OP_CONST_FLOAT:
104:     PUSH(value_float(*(double*)bc->code[ip].ptr)); ip++; DISPATCH();
105: 
106: label_OP_CONST_STRING:
107:     PUSH(value_string((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
108: 
109: label_OP_CONST_BOOL:
110:     PUSH(value_bool(bc->code[ip].arg)); ip++; DISPATCH();
111: 
112: label_OP_LOAD_VAR:
113:     PUSH(var_get((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
114: 
115: label_OP_STORE_VAR:
116:     var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();
117: 
118: label_OP_LOAD_LOCAL: {
119:     int idx = bc->code[ip].arg;
120:     if (csp > 0) {
121:         CallFrame *cf = &call_stack[csp-1];
122:         if (cf->local_values && idx < cf->local_count) PUSH(cf->local_values[idx]);
123:         else PUSH(value_int(0));
124:     } else PUSH(var_get((const char*)bc->code[ip].ptr));
125:     ip++; DISPATCH();
126: }
127: 
128: label_OP_STORE_LOCAL: {
129:     int idx = bc->code[ip].arg; Value v = POP();
130:     if (csp > 0) {
131:         CallFrame *cf = &call_stack[csp-1];
132:         if (cf->local_values && idx < cf->local_count) cf->local_values[idx] = v;
133:     } else var_set((const char*)bc->code[ip].ptr, v);
134:     ip++; DISPATCH();
135: }
136: 
137: label_OP_ADD: {
138:     Value b = POP(), a = POP();
139:     if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) + AS_INT(b)));
140:     else if (GET_TYPE(a) == VAL_FLOAT && GET_TYPE(b) == VAL_FLOAT) PUSH(value_float(AS_FLOAT(a) + AS_FLOAT(b)));
141:     else if (GET_TYPE(a) == VAL_STRING && GET_TYPE(b) == VAL_STRING) {
142:         char *s = malloc(strlen(AS_STRING(a)) + strlen(AS_STRING(b)) + 1);
143:         strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b)); PUSH(value_string(s));
144:     } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
145:         List *l1 = AS_LIST(a), *l2 = AS_LIST(b); List *res = list_new(l1->size + l2->size);
146:         for (int i = 0; i < l1->size; i++) { res->items[i] = l1->items[i]; if (GET_TYPE(res->items[i]) == VAL_LIST) list_incref(AS_LIST(res->items[i])); if (GET_TYPE(res->items[i]) == VAL_DICT) dict_incref(AS_DICT(res->items[i])); }
147:         for (int i = 0; i < l2->size; i++) { res->items[l1->size + i] = l2->items[i]; if (GET_TYPE(res->items[l1->size + i]) == VAL_LIST) list_incref(AS_LIST(res->items[l1->size + i])); if (GET_TYPE(res->items[l1->size + i]) == VAL_DICT) dict_incref(AS_DICT(res->items[l1->size + i])); }
148:         res->size = l1->size + l2->size; PUSH(value_list(res));
149:     } else {
150:         double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
151:         PUSH(value_float(da + db));
152:     }
153:     ip++; DISPATCH();
154: }
155: 
156: label_OP_SUB: {
157:     Value b = POP(), a = POP();
158:     if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) - AS_INT(b)));
159:     else PUSH(value_float(val_to_int_or_float(a) - val_to_int_or_float(b)));
160:     ip++; DISPATCH();
161: }
162: 
163: label_OP_MUL: {
164:     Value b = POP(), a = POP();
165:     if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) * AS_INT(b)));
166:     else PUSH(value_float(val_to_int_or_float(a) * val_to_int_or_float(b)));
167:     ip++; DISPATCH();
168: }
169: 
170: label_OP_DIV: {
171:     Value b = POP(), a = POP();
172:     double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
173:     if (db == 0.0) { printf("Error: Division by zero\n"); PUSH(value_float(0)); }
174:     else PUSH(value_float(da / db));
175:     ip++; DISPATCH();
176: }
177: 
178: label_OP_MOD: {
179:     Value b = POP(), a = POP();
180:     if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
181:         if (AS_INT(b) == 0) { printf("Error: Modulo by zero\n"); PUSH(value_int(0)); }
182:         else PUSH(value_int(AS_INT(a) % AS_INT(b)));
183:     } else {
184:         double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
185:         if (db == 0.0) { printf("Error: Modulo by zero\n"); PUSH(value_float(0)); }
186:         else PUSH(value_float(fmod(da, db)));
187:     }
188:     ip++; DISPATCH();
189: }
190: 
191: label_OP_POW: {
192:     Value b = POP(), a = POP();
193:     PUSH(value_float(pow(val_to_int_or_float(a), val_to_int_or_float(b))));
194:     ip++; DISPATCH();
195: }
196: 
197: label_OP_NEG: {
198:     Value a = POP();
199:     if (GET_TYPE(a) == VAL_INT) PUSH(value_int(-AS_INT(a)));
200:     else PUSH(value_float(-val_to_int_or_float(a)));
201:     ip++; DISPATCH();
202: }
203: 
204: label_OP_LT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) < val_to_int_or_float(b))); ip++; DISPATCH(); }
205: label_OP_GT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) > val_to_int_or_float(b))); ip++; DISPATCH(); }
206: label_OP_LTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) <= val_to_int_or_float(b))); ip++; DISPATCH(); }
207: label_OP_GTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) >= val_to_int_or_float(b))); ip++; DISPATCH(); }
208: 
209: label_OP_EQ: {
210:     Value b = POP(), a = POP(); int res = 0;
211:     if (GET_TYPE(a) == GET_TYPE(b)) {
212:         switch (GET_TYPE(a)) {
213:             case VAL_INT: res = AS_INT(a) == AS_INT(b); break;
214:             case VAL_FLOAT: res = AS_FLOAT(a) == AS_FLOAT(b); break;
215:             case VAL_BOOL: res = AS_BOOL(a) == AS_BOOL(b); break;
216:             case VAL_STRING: res = strcmp(AS_STRING(a), AS_STRING(b)) == 0; break;
217:             default: res = (a == b); break;
218:         }
219:     } else res = (val_to_int_or_float(a) == val_to_int_or_float(b));
220:     PUSH(value_bool(res)); ip++; DISPATCH();
221: }
222: 
223: label_OP_NEQ: {
224:     Value b = POP(), a = POP(); PUSH(value_bool(!val_eq(a, b))); ip++; DISPATCH();
225: }
226: 
227: label_OP_AND: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) && val_to_bool(b))); ip++; DISPATCH(); }
228: label_OP_OR: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) || val_to_bool(b))); ip++; DISPATCH(); }
229: label_OP_NOT: PUSH(value_bool(!val_to_bool(POP()))); ip++; DISPATCH();
230: 
231: label_OP_STRING_CONCAT: {
232:     Value b = POP(), a = POP();
233:     char *s = malloc(strlen(AS_STRING(a)) + strlen(AS_STRING(b)) + 1);
234:     strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b)); PUSH(value_string(s));
235:     ip++; DISPATCH();
236: }
237: 
238: label_OP_JUMP:
239:     ip = bc->code[ip].arg - base_offset; DISPATCH();
240: 
241: label_OP_JUMP_IF_FALSE:
242:     if (!val_to_bool(vm_stack[sp-1])) ip = bc->code[ip].arg - base_offset; else ip++;
243:     DISPATCH();
244: 
245: label_OP_JUMP_IF_TRUE:
246:     if (val_to_bool(vm_stack[sp-1])) ip = bc->code[ip].arg - base_offset; else ip++;
247:     DISPATCH();
248: 
249: label_OP_CALL_BUILTIN_LEN: {
250:     Value v = POP(); int len = 0;
251:     if (GET_TYPE(v) == VAL_LIST) len = AS_LIST(v)->size;
252:     else if (GET_TYPE(v) == VAL_STRING) len = (int)strlen(AS_STRING(v));
253:     else if (GET_TYPE(v) == VAL_DICT) len = AS_DICT(v)->size;
254:     PUSH(value_int(len)); ip++; DISPATCH();
255: }
256: 
257: label_OP_CALL_BUILTIN_RANGE: {
258:     int n = val_to_int(POP()); List *l = list_new(n < 0 ? 0 : n);
259:     for (int i = 0; i < n; i++) l->items[i] = value_int(i);
260:     PUSH(value_list(l)); ip++; DISPATCH();
261: }
262: 
263: label_OP_CALL_BUILTIN_TYPE:
264:     PUSH(value_string(type_name(GET_TYPE(POP())))); ip++; DISPATCH();
265: 
266: label_OP_BUILTIN_APPEND: {
267:     Value item = POP(), lv = POP();
268:     if (GET_TYPE(lv) == VAL_LIST) list_push(AS_LIST(lv), item);
269:     PUSH(lv); ip++; DISPATCH();
270: }
271: 
272: label_OP_BUILTIN_INPUT: {
273:     if (bc->code[ip].arg == 1) { Value p = POP(); if (GET_TYPE(p) == VAL_STRING) { printf("%s", AS_STRING(p)); fflush(stdout); } }
274:     char buf[4096]; if (fgets(buf, sizeof(buf), stdin)) {
275:         int len = (int)strlen(buf); while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
276:         PUSH(value_string(strdup(buf)));
277:     } else PUSH(value_string(""));
278:     ip++; DISPATCH();
279: }
280: 
281: label_OP_BUILTIN_SHOW: {
282:     int n = bc->code[ip].arg; if (n == 0) printf("\n");
283:     else { for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); } printf("\n"); }
284:     ip++; DISPATCH();
285: }
286: 
287: label_OP_BUILTIN_WRITE: {
288:     int n = bc->code[ip].arg; for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); }
289:     fflush(stdout); ip++; DISPATCH();
290: }
291: 
292: label_OP_LIST_LITERAL: {
293:     int n = bc->code[ip].arg; List *l = list_new(n);
294:     for (int i = n-1; i >= 0; i--) l->items[i] = POP();
295:     PUSH(value_list(l)); ip++; DISPATCH();
296: }
297: 
298: label_OP_DICT_LITERAL: {
299:     int n = bc->code[ip].arg; Dict *d = dict_new(n);
300:     for (int i = 0; i < n; i++) { Value v = POP(), k = POP(); dict_set(d, AS_STRING(k), v); }
301:     PUSH(value_dict(d)); ip++; DISPATCH();
302: }
303: 
304: label_OP_INDEX: {
305:     Value idx = POP(), coll = POP();
306:     if (GET_TYPE(coll) == VAL_LIST) {
307:         int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
308:         if (i < 0 || i >= AS_LIST(coll)->size) PUSH(value_int(0));
309:         else PUSH(AS_LIST(coll)->items[i]);
310:     } else if (GET_TYPE(coll) == VAL_STRING) {
311:         int i = val_to_int(idx); int slen = (int)strlen(AS_STRING(coll)); if (i < 0) i += slen;
312:         if (i < 0 || i >= slen) PUSH(value_int(0));
313:         else { char b[2] = {AS_STRING(coll)[i], 0}; PUSH(value_string(strdup(b))); }
314:     } else if (GET_TYPE(coll) == VAL_DICT) {
315:         int found = 0; for (int i = 0; i < AS_DICT(coll)->size; i++) {
316:             if (strcmp(AS_DICT(coll)->keys[i], AS_STRING(idx)) == 0) { PUSH(AS_DICT(coll)->values[i]); found = 1; break; }
317:         }
318:         if (!found) PUSH(value_int(0));
319:     } else PUSH(value_int(0));
320:     ip++; DISPATCH();
321: }
322: 
323: label_OP_SLICE: {
324:     Value eV = POP(), sV = POP(), src = POP();
325:     int slen = GET_TYPE(src) == VAL_STRING ? (int)strlen(AS_STRING(src)) : (GET_TYPE(src) == VAL_LIST ? AS_LIST(src)->size : 0);
326:     int s = (GET_TYPE(sV) == VAL_INT && AS_INT(sV) == -1) ? 0 : val_to_int(sV);
327:     int e = (GET_TYPE(eV) == VAL_INT && AS_INT(eV) == -1) ? slen : val_to_int(eV);
328:     if (s < 0) s += slen; if (e < 0) e += slen; if (s < 0) s = 0; if (e > slen) e = slen; if (s > e) s = e;
329:     if (GET_TYPE(src) == VAL_STRING) { char *sub = malloc(e - s + 1); strncpy(sub, AS_STRING(src) + s, e - s); sub[e-s] = 0; PUSH(value_string(sub)); }
330:     else if (GET_TYPE(src) == VAL_LIST) {
331:         List *rl = list_new(e - s); for (int i = 0; i < e - s; i++) { rl->items[i] = AS_LIST(src)->items[s + i]; if (GET_TYPE(rl->items[i]) == VAL_LIST) list_incref(AS_LIST(rl->items[i])); if (GET_TYPE(rl->items[i]) == VAL_DICT) dict_incref(AS_DICT(rl->items[i])); }
332:         PUSH(value_list(rl));
333:     } else PUSH(value_int(0));
334:     ip++; DISPATCH();
335: }
336: 
337: label_OP_STORE_INDEX: {
338:     Value v = POP(), idx = POP(), coll = POP();
339:     if (GET_TYPE(coll) == VAL_LIST) {
340:         int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
341:         if (i >= 0 && i < AS_LIST(coll)->size) AS_LIST(coll)->items[i] = v;
342:     } else if (GET_TYPE(coll) == VAL_DICT && GET_TYPE(idx) == VAL_STRING) dict_set(AS_DICT(coll), AS_STRING(idx), v);
343:     ip++; DISPATCH();
344: }
345: 
346: label_OP_FUNC_DEF: ip++; DISPATCH();
347: 
348: label_OP_CALL: {
349:     FuncEntry *fn = func_get((const char*)bc->code[ip].ptr);
350:     if (fn) {
351:         if (csp >= CALL_STACK_SIZE-1) exit(1);
352:         CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
353:         cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
354:         cf->pending_fn = fn; cf->pending_closure = NULL; cf->local_count = fn->local_count;
355:         if (cf->local_count > 0) {
356:             if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) cf->local_values[i] = value_none(); }
357:             else cf->local_values = calloc(cf->local_count, sizeof(Value));
358:         } else cf->local_values = NULL;
359:         if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
360:         for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
361:         
362:         Value rv = run_bytecode(fn->body, 0);
363: 
364:         /* Restore caller state */
365:         csp--;
366:         cf = &call_stack[csp];
367:         if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
368:         if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
369:         if (cf->local_values && cf->local_count > 16) free(cf->local_values);
370:         bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
371:         vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
372:         PUSH(rv); DISPATCH();
373:     } else {
374:         Value v = var_get((const char*)bc->code[ip].ptr);
375:         if (GET_TYPE(v) == VAL_CLOSURE) {
376:             Closure *c = AS_CLOSURE(v); if (csp >= CALL_STACK_SIZE-1) exit(1);
377:             CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
378:             cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
379:             cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
380:             for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
381:             Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
382:             for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
383:             cf->param_count = c->param_count < 2 ? c->param_count : 2;
384:             if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
385:             if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
386:             for (int i=0; i<c->param_count; i++) var_set_let(c->params[i], ab[i]);
387:             if (c->param_count > 8) free(ab);
388:             
389:             Value rv = run_bytecode(c->body, 0);
390:             
391:             csp--;
392:             cf = &call_stack[csp];
393:             var_restore_shadow(cf->old_shadow_list);
394:             if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
395:             bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
396:             vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
397:             PUSH(rv); DISPATCH();
398:         }
399:         ip++; DISPATCH();
400:     }
401: }
402: 
403: label_OP_RETURN: {
404:     total_returns++; Value rv = (sp > base_sp) ? POP() : value_none(); 
405:     return rv;
406: }
407: 
408: label_OP_FOR_EACH: {
409:     Value iter = POP(); const char *vn = (const char*)bc->code[ip].ptr;
410:     int bs = ip + 1; int be = bc->code[ip].arg;
411:     if (GET_TYPE(iter) == VAL_LIST) {
412:         for (int i=0; i<AS_LIST(iter)->size; i++) {
413:             vm_loop_skip = 0; 
414:             int found_local = -1;
415:             if (csp > 0) {
416:                 CallFrame *outer = &call_stack[csp-1];
417:                 if (outer->pending_fn) {
418:                     for (int j=0; j<outer->pending_fn->local_count; j++) {
419:                         if (strcmp(outer->pending_fn->local_names[j], vn) == 0) { found_local = j; break; }
420:                     }
421:                 }
422:             }
423:             if (found_local >= 0) call_stack[csp-1].local_values[found_local] = AS_LIST(iter)->items[i];
424:             else var_set(vn, AS_LIST(iter)->items[i]); 
425: 
426:             int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
427:             Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
428:             Value was_rv = run_bytecode(&bbc, bs);
429:             if (vm_exception_active) break;
430:             sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
431:             if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
432:             if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
433:         }
434:     } else if (GET_TYPE(iter) == VAL_STRING) {
435:         int len = strlen(AS_STRING(iter)); for (int i=0; i<len; i++) {
436:             char b[2] = {AS_STRING(iter)[i], 0}; var_set(vn, value_string(strdup(b))); 
437:             int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
438:             Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
439:             Value was_rv = run_bytecode(&bbc, bs);
440:             if (vm_exception_active) break;
441:             sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
442:             if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
443:             if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
444:         }
445:     } else if (GET_TYPE(iter) == VAL_DICT) {
446:         for (int i=0; i<AS_DICT(iter)->size; i++) {
447:             var_set(vn, value_string(AS_DICT(iter)->keys[i])); 
448:             int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
449:             Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
450:             Value was_rv = run_bytecode(&bbc, bs);
451:             if (vm_exception_active) break;
452:             sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
453:             if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
454:             if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
455:         }
456:     }
457:     ip = be - base_offset; DISPATCH();
458: }
459: 
460: label_OP_FOR_EACH_PAIR: {
461:     Value iter = POP(); const char *kn = (const char*)bc->code[ip].ptr; const char *vn = (const char*)bc->code[ip+1].ptr;
462:     int bs = ip + 2; int be = bc->code[ip].arg;
463:     if (GET_TYPE(iter) == VAL_LIST) {
464:         for (int i=0; i<AS_LIST(iter)->size; i++) {
465:             var_set(kn, value_int(i)); var_set(vn, AS_LIST(iter)->items[i]); 
466:             int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
467:             Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
468:             Value was_rv = run_bytecode(&bbc, bs);
469:             if (vm_exception_active) break;
470:             sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
471:             if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
472:             if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
473:         }
474:     } else if (GET_TYPE(iter) == VAL_DICT) {
475:         for (int i=0; i<AS_DICT(iter)->size; i++) {
476:             var_set(kn, value_string(AS_DICT(iter)->keys[i])); var_set(vn, AS_DICT(iter)->values[i]); 
477:             int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
478:             Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
479:             Value was_rv = run_bytecode(&bbc, bs);
480:             if (vm_exception_active) break;
481:             sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
482:             if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
483:             if (vm_loop_skip) { vm_loop_skip = 0; continue; } if (vm_loop_stop) { vm_loop_stop = 0; break; }
484:         }
485:     }
486:     ip = be - base_offset; DISPATCH();
487: }
488: 
489: label_OP_STOP: vm_loop_stop = 1; ip++; DISPATCH();
490: label_OP_SKIP: vm_loop_skip = 1; ip++; DISPATCH();
491: label_OP_POP: POP(); ip++; DISPATCH();
492: label_OP_PUSH_SCOPE: var_push_scope(); ip++; DISPATCH();
493: label_OP_POP_SCOPE: var_pop_scope(); ip++; DISPATCH();
494: label_OP_STORE_LET: var_set_let((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();
495: 
496: label_OP_DICT_KEYS: {
497:     Value d = POP(); if (GET_TYPE(d) != VAL_DICT) { PUSH(value_int(0)); }
498:     else { List *l = list_new(AS_DICT(d)->size); for (int i=0; i<AS_DICT(d)->size; i++) l->items[i] = value_string(AS_DICT(d)->keys[i]); PUSH(value_list(l)); }
499:     ip++; DISPATCH();
500: }
501: label_OP_DICT_VALUES: {
502:     Value d = POP(); if (GET_TYPE(d) != VAL_DICT) { PUSH(value_int(0)); }
503:     else { List *l = list_new(AS_DICT(d)->size); for (int i=0; i<AS_DICT(d)->size; i++) { l->items[i] = AS_DICT(d)->values[i]; if (GET_TYPE(l->items[i]) == VAL_LIST) list_incref(AS_LIST(l->items[i])); if (GET_TYPE(l->items[i]) == VAL_DICT) dict_incref(AS_DICT(l->items[i])); } PUSH(value_list(l)); }
504:     ip++; DISPATCH();
505: }
506: label_OP_DICT_HAS: {
507:     Value k = POP(), d = POP(); int f = 0; if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { f = 1; break; } }
508:     PUSH(value_bool(f)); ip++; DISPATCH();
509: }
510: label_OP_DICT_REMOVE: {
511:     Value k = POP(), d = POP(); if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { for (int j=i; j<AS_DICT(d)->size-1; j++) { AS_DICT(d)->keys[j] = AS_DICT(d)->keys[j+1]; AS_DICT(d)->values[j] = AS_DICT(d)->values[j+1]; } AS_DICT(d)->size--; break; } }
512:     PUSH(d); ip++; DISPATCH();
513: }
514: label_OP_DICT_MERGE: {
515:     Value d2 = POP(), d1 = POP(); if (GET_TYPE(d1) == VAL_DICT && GET_TYPE(d2) == VAL_DICT) { for (int i=0; i<AS_DICT(d2)->size; i++) dict_set(AS_DICT(d1), AS_DICT(d2)->keys[i], AS_DICT(d2)->values[i]); }
516:     PUSH(d1); ip++; DISPATCH();
517: }
518: 
519: label_OP_MATH_ABS: {
520:     Value v = POP(); if (GET_TYPE(v) == VAL_INT) PUSH(value_int(abs(AS_INT(v)))); else if (GET_TYPE(v) == VAL_FLOAT) PUSH(value_float(fabs(AS_FLOAT(v)))); else PUSH(value_int(0));
521:     ip++; DISPATCH();
522: }
523: label_OP_MATH_MIN: {
524:     int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) < val_to_int_or_float(best)) best = v; }
525:     PUSH(best); ip++; DISPATCH();
526: }
527: label_OP_MATH_MAX: {
528:     int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) > val_to_int_or_float(best)) best = v; }
529:     PUSH(best); ip++; DISPATCH();
530: }
531: label_OP_MATH_ROUND: PUSH(value_int((int)round(val_to_int_or_float(POP())))); ip++; DISPATCH();
532: label_OP_MATH_FLOOR: PUSH(value_int((int)floor(val_to_int_or_float(POP())))); ip++; DISPATCH();
533: label_OP_MATH_CEIL: PUSH(value_int((int)ceil(val_to_int_or_float(POP())))); ip++; DISPATCH();
534: label_OP_MATH_SQRT: PUSH(value_float(sqrt(val_to_int_or_float(POP())))); ip++; DISPATCH();
535: 
536: label_OP_STR_UPPER: {
537:     Value s = POP(); if (GET_TYPE(s) == VAL_STRING) { char *u = strdup(AS_STRING(s)); for (char *p=u; *p; p++) *p = toupper(*p); PUSH(value_string(u)); } else PUSH(s);
538:     ip++; DISPATCH();
539: }
540: label_OP_STR_LOWER: {
541:     Value s = POP(); if (GET_TYPE(s) == VAL_STRING) { char *l = strdup(AS_STRING(s)); for (char *p=l; *p; p++) *p = tolower(*p); PUSH(value_string(l)); } else PUSH(s);
542:     ip++; DISPATCH();
543: }
544: label_OP_STR_TRIM: {
545:     Value s = POP(); if (GET_TYPE(s) == VAL_STRING) {
546:         const char *st = AS_STRING(s); while (*st && isspace(*st)) st++;
547:         char *ed = (char*)st + strlen(st) - 1; while (ed > st && isspace(*ed)) ed--;
548:         int len = ed - st + 1; if (len < 0) len = 0; char *res = malloc(len + 1); strncpy(res, st, len); res[len] = 0; PUSH(value_string(res));
549:     } else PUSH(s);
550:     ip++; DISPATCH();
551: }
552: label_OP_STR_FIND: {
553:     Value sub = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING) { char *p = strstr(AS_STRING(s), AS_STRING(sub)); PUSH(value_int(p ? (int)(p - AS_STRING(s)) : -1)); } else PUSH(value_int(-1));
554:     ip++; DISPATCH();
555: }
556: label_OP_STR_REPLACE: {
557:     Value r = POP(), sub = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING && GET_TYPE(r) == VAL_STRING) PUSH(value_string(str_replace(AS_STRING(s), AS_STRING(sub), AS_STRING(r)))); else PUSH(s);
558:     ip++; DISPATCH();
559: }
560: label_OP_STR_STARTSWITH: {
561:     Value p = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(p) == VAL_STRING) PUSH(value_bool(strncmp(AS_STRING(s), AS_STRING(p), strlen(AS_STRING(p))) == 0)); else PUSH(value_bool(0));
562:     ip++; DISPATCH();
563: }
564: label_OP_STR_ENDSWITH: {
565:     Value su = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(su) == VAL_STRING) { int sl = strlen(AS_STRING(s)), sul = strlen(AS_STRING(su)); PUSH(value_bool(sl >= sul && strcmp(AS_STRING(s) + sl - sul, AS_STRING(su)) == 0)); } else PUSH(value_bool(0));
566:     ip++; DISPATCH();
567: }
568: label_OP_STR_SPLIT: {
569:     Value sep = POP(), s = POP(); if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sep) == VAL_STRING) {
570:         List *l = list_new(8); char *c = strdup(AS_STRING(s)), *t = strtok(c, AS_STRING(sep));
571:         while (t) { list_push(l, value_string(strdup(t))); t = strtok(NULL, AS_STRING(sep)); } PUSH(value_list(l)); free(c);
572:     } else PUSH(value_list(list_new(0)));
573:     ip++; DISPATCH();
574: }
575: 
576: label_OP_LIST_PUSH: { Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) list_push(AS_LIST(l), v); PUSH(l); ip++; DISPATCH(); }
577: label_OP_LIST_POP: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST && AS_LIST(l)->size > 0) PUSH(AS_LIST(l)->items[--AS_LIST(l)->size]); else PUSH(value_int(0)); ip++; DISPATCH(); }
578: label_OP_LIST_INSERT: {
579:     Value v = POP(), idx = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
580:         int i = val_to_int(idx); if (i < 0) i += AS_LIST(l)->size; if (i >= 0 && i <= AS_LIST(l)->size) {
581:             list_push(AS_LIST(l), v); for (int j=AS_LIST(l)->size-1; j>i; j--) AS_LIST(l)->items[j] = AS_LIST(l)->items[j-1]; AS_LIST(l)->items[i] = v;
582:         }
583:     } PUSH(l); ip++; DISPATCH();
584: }
585: label_OP_LIST_REMOVE: {
586:     Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
587:         int idx = -1; for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { idx = i; break; }
588:         if (idx >= 0) { for (int i=idx; i<AS_LIST(l)->size-1; i++) AS_LIST(l)->items[i] = AS_LIST(l)->items[i+1]; AS_LIST(l)->size--; }
589:     } PUSH(l); ip++; DISPATCH();
590: }
591: label_OP_LIST_CONTAINS: {
592:     Value v = POP(), l = POP(); int f = 0; if (GET_TYPE(l) == VAL_LIST) for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { f = 1; break; }
593:     PUSH(value_bool(f)); ip++; DISPATCH();
594: }
595: label_OP_LIST_SORT: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST) qsort(AS_LIST(l)->items, AS_LIST(l)->size, sizeof(Value), val_compare); PUSH(l); ip++; DISPATCH(); }
596: label_OP_LIST_REVERSE: {
597:     Value l = POP(); if (GET_TYPE(l) == VAL_LIST) { int n = AS_LIST(l)->size; for (int i=0; i<n/2; i++) { Value t = AS_LIST(l)->items[i]; AS_LIST(l)->items[i] = AS_LIST(l)->items[n-1-i]; AS_LIST(l)->items[n-1-i] = t; } }
598:     PUSH(l); ip++; DISPATCH();
599: }
600: 
601: label_OP_FILE_READ: {
602:     Value p = POP(); if (GET_TYPE(p) == VAL_STRING) {
603:         FILE *f = fopen(AS_STRING(p), "r"); if (f) { fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET); char *b = malloc(sz+1); fread(b,1,sz,f); b[sz]=0; fclose(f); PUSH(value_string(b)); } else PUSH(value_string(""));
604:     } else PUSH(value_string(""));
605:     ip++; DISPATCH();
606: }
607: label_OP_FILE_WRITE: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "w"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }
608: label_OP_FILE_APPEND: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "a"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }
609: 
610: label_OP_FSTRING: {
611:     int n = bc->code[ip].arg; char *res = strdup(""); for (int i=0; i<n; i++) { Value v = POP(); char *vs = val_to_str(v); char *next = malloc(strlen(res) + strlen(vs) + 1); strcpy(next, vs); strcat(next, res); free(res); res = next; }
612:     PUSH(value_string(res)); ip++; DISPATCH();
613: }
614: 
615: label_OP_LIST_COMP: ip++; DISPATCH();
616: 
617: label_OP_THROW: vm_exception_value = POP(); vm_exception_active = 1; DISPATCH();
618: 
619: label_OP_TRY_START: {
620:     if (hsp >= HANDLER_STACK_SIZE) exit(1);
621:     handler_stack[hsp].catch_ip = bc->code[ip].arg; handler_stack[hsp].csp = csp; handler_stack[hsp].sp = sp; handler_stack[hsp].shadow_list = let_shadow_list;
622:     hsp++; ip++; DISPATCH();
623: }
624: label_OP_TRY_END: if (hsp > 0) hsp--; ip++; DISPATCH();
625: 
626: label_OP_LAMBDA: {
627:     int pc = bc->code[ip].arg; Bytecode *body = (Bytecode*)bc->code[ip].ptr; char **pnames = malloc(sizeof(char*)*(pc>0?pc:1));
628:     for (int i=pc-1; i>=0; i--) pnames[i] = (char*)AS_STRING(POP()); Closure *c = closure_new(pc, pnames, body); free(pnames);
629:     int cnt = 0; for (LetShadow *s = let_shadow_list; s; s = s->next) cnt++;
630:     if (cnt > 0) { c->captured_count = cnt; c->captured_names = malloc(sizeof(const char*)*cnt); c->captured_values = malloc(sizeof(Value)*cnt); int i=0; for (LetShadow *s = let_shadow_list; s; s = s->next) { c->captured_names[i] = s->name; c->captured_values[i] = var_get(s->name); i++; } }
631:     PUSH(value_closure(c)); ip++; DISPATCH();
632: }
633: 
634: label_OP_CALL_VALUE: {
635:     Value callee = POP(); if (GET_TYPE(callee) == VAL_CLOSURE) {
636:         Closure *c = AS_CLOSURE(callee); if (csp >= CALL_STACK_SIZE-1) exit(1);
637:         CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
638:         cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
639:         cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
640:         for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
641:         Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
642:         for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
643:         cf->param_count = c->param_count < 2 ? c->param_count : 2;
644:         if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
645:         if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
646:         for (int i=0; i<c->param_count; i++) var_set_let(c->params[i], ab[i]);
647:         if (c->param_count > 8) free(ab);
648:         
649:         Value rv = run_bytecode(c->body, 0);
650:         
651:         csp--;
652:         cf = &call_stack[csp];
653:         var_restore_shadow(cf->old_shadow_list);
654:         if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
655:         bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
656:         vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
657:         PUSH(rv); DISPATCH();
658:     } else if (GET_TYPE(callee) == VAL_STRING) {
659:         FuncEntry *fn = func_get(AS_STRING(callee)); if (!fn) { PUSH(value_none()); ip++; DISPATCH(); }
660:         if (csp >= CALL_STACK_SIZE-1) exit(1);
661:         CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
662:         cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
663:         cf->pending_fn = fn; cf->pending_closure = NULL;
664:         cf->local_count = fn->local_count; if (cf->local_count > 0) {
665:             if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) { cf->local_values[i] = value_none(); } }
666:             else cf->local_values = calloc(cf->local_count, sizeof(Value));
667:         } else cf->local_values = NULL;
668:         if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
669:         for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
670:         
671:         Value rv = run_bytecode(fn->body, 0);
672:         
673:         csp--;
674:         cf = &call_stack[csp];
675:         if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
676:         if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
677:         if (cf->local_values && cf->local_count > 16) free(cf->local_values);
678:         bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
679:         vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
680:         PUSH(rv); DISPATCH();
681:     } else { PUSH(value_none()); ip++; DISPATCH(); }
682: }
683: 
684: label_EXIT_VM:
685:     return value_none();
686: }
687: 
The above content shows the entire, complete file contents of the requested file.


=== BLOCK ===

Now let me check for additional files that might contain the register-based VM and the GC implementation:

=== BLOCK ===

Created At: 2026-06-04T14:15:23Z
Completed At: 2026-06-04T14:15:25Z
No results found

=== BLOCK ===

Created At: 2026-06-04T14:15:25Z
Completed At: 2026-06-04T14:15:26Z
Found 13 results
arena.c
arena.h
ast.c
glowlang.h
ir.c
ir.h
jit.c
jit.h
lsp.c
lsp.h
opt.c
opt.h
run_bytecode_optimized.c

=== BLOCK ===

Created At: 2026-06-04T14:15:26Z
Completed At: 2026-06-04T14:15:30Z
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":59,"LineContent":"static void gc_mark_value(Value v);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":60,"LineContent":"static void gc_mark_object(GCObject *obj);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":62,"LineContent":"static void gc_collect(void);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":546,"LineContent":"static void gc_mark_object(GCObject *obj) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":550,"LineContent":"        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i\u003cl-\u003esize; i++) gc_mark_value(l-\u003eitems[i]); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":551,"LineContent":"        case OBJ_ITERATOR: { Iterator *it = (Iterator*)obj; gc_mark_value(it-\u003eiterable); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":552,"LineContent":"        case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i\u003cd-\u003esize; i++) { gc_mark_value(d-\u003ekeys[i]); gc_mark_value(d-\u003evalues[i]); } break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":557,"LineContent":"            gc_mark_object((GCObject*)c-\u003emethods); "}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":558,"LineContent":"            gc_mark_object((GCObject*)c-\u003eroot_shape); "}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":559,"LineContent":"            if (c-\u003estable_shape) gc_mark_object((GCObject*)c-\u003estable_shape);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":564,"LineContent":"            if (s-\u003eparent) gc_mark_object((GCObject*)s-\u003eparent);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":565,"LineContent":"            gc_mark_object((GCObject*)s-\u003etransitions);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":570,"LineContent":"            gc_mark_object((GCObject*)ins-\u003eklass); "}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":571,"LineContent":"            gc_mark_object((GCObject*)ins-\u003eproperties); "}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":574,"LineContent":"        case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; gc_mark_value(bm-\u003einstance); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":577,"LineContent":"            if (rn-\u003eis_leaf) gc_mark_value(rn-\u003eleaf.flat_string);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":579,"LineContent":"                gc_mark_object((GCObject*)rn-\u003einternal.left);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":580,"LineContent":"                gc_mark_object((GCObject*)rn-\u003einternal.right);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":588,"LineContent":"static void gc_mark_value(Value v) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":590,"LineContent":"        if (v \u0026 MASK_PAYLOAD) gc_mark_object((GCObject*)(uintptr_t)(v \u0026 MASK_PAYLOAD));"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":596,"LineContent":"    for (int i=0; i\u003cglobal_table.count; i++) gc_mark_value(global_table.values[i]);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":603,"LineContent":"            for (int j=0; j\u003clim; j++) gc_mark_value(stk[j]);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":605,"LineContent":"        gc_mark_value(*(active_stacks[i].pending_exc_ptr));"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":635,"LineContent":"    for (int i=0; i\u003cglobal_table.count; i++) gc_mark_value(global_table.values[i]);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":642,"LineContent":"            for (int j=0; j\u003clim; j++) gc_mark_value(stk[j]);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":644,"LineContent":"        gc_mark_value(*(active_stacks[i].pending_exc_ptr));"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":648,"LineContent":"    while (rs) { gc_mark_object(rs-\u003eobj); rs = rs-\u003enext; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":675,"LineContent":"static void gc_collect(void) { gc_full_collect(); }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":52,"LineContent":"static void gc_mark_value(Value v);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":53,"LineContent":"static void gc_mark_object(GCObject *obj);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":54,"LineContent":"static void gc_collect(void);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":253,"LineContent":"static void gc_mark_object(GCObject *obj) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":257,"LineContent":"        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i\u003cl-\u003esize; i++) gc_mark_value(l-\u003eitems[i]); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":258,"LineContent":"        case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i\u003cd-\u003esize; i++) gc_mark_value(d-\u003evalues[i]); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":259,"LineContent":"        case OBJ_CLASS: { Class *c = (Class*)obj; gc_mark_object((GCObject*)c-\u003emethods); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":260,"LineContent":"        case OBJ_INSTANCE: { Instance *ins = (Instance*)obj; gc_mark_object((GCObject*)ins-\u003eklass); if(ins-\u003efields) for(int i=0; i\u003c=ins-\u003eshape-\u003eoffset; i++) gc_mark_value(ins-\u003efields[i]); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":261,"LineContent":"        case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; gc_mark_value(bm-\u003einstance); break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":265,"LineContent":"static void gc_mark_value(Value v) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":267,"LineContent":"        if (v \u0026 MASK_PAYLOAD) gc_mark_object((GCObject*)(uintptr_t)(v \u0026 MASK_PAYLOAD));"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":270,"LineContent":"static void gc_collect(void) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":271,"LineContent":"    for (int i=0; i\u003cVAR_TABLE_SIZE; i++) if (var_table[i].used) gc_mark_value(var_table[i].value);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":276,"LineContent":"        if (stk) for (int j=0; j\u003cstk_sp; j++) gc_mark_value(stk[j]);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":278,"LineContent":"        while (s) { gc_mark_value(s-\u003eold_value); s = s-\u003enext; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":279,"LineContent":"        gc_mark_value(*(active_stacks[i].pending_exc_ptr));"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.bak","LineNumber":297,"LineContent":"    if (gc_allocated \u003e gc_threshold) gc_collect();"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.final","LineNumber":52,"LineContent":"static void gc_mark_value(Value v);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.final","LineNumber":53,"LineContent":"static void gc_mark_object(GCObject *obj);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.final","LineNumber":54,"LineContent":"static void gc_collect(void);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.final","LineNumber":228,"LineContent":"static void gc_mark_object(GCObject *obj) {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y.final","LineNumber":233,"LineContent":"        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i\u003cl-\u003esize; i++) gc_mark_value(l-\u003eitems[i]); break; }"}
(...47 more results not shown)

=== BLOCK ===

Created At: 2026-06-04T14:15:30Z
Completed At: 2026-06-04T14:15:30Z
No results found

=== BLOCK ===

Let me look at the register-based VM and the main glowlang.y file:

=== BLOCK ===

Created At: 2026-06-04T14:15:37Z
Completed At: 2026-06-04T14:15:38Z
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1079,"LineContent":"            case OP_SPAWN: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1084,"LineContent":"            case OP_SET_PROPERTY: escapes[pc-\u003er1] = 1; break;"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1118,"LineContent":"        if (pc-\u003eop == OP_SPAWN || pc-\u003eop == OP_TRY_START || pc-\u003eop == OP_LAMBDA) return false;"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1246,"LineContent":"            bc_emit_reg(compiling_bc, OP_ITER_START, iter_reg, coll_reg, 0);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1306,"LineContent":"            bc_emit(compiling_bc, (last_compiling_class || last_compiling_trait) ? OP_METHOD_DEF : OP_FUNC_DEF, 0, (intptr_t)n-\u003ename); "}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1330,"LineContent":"            bc_emit(compiling_bc, OP_CLASS_DEF, n-\u003eparent_name ? 1 : 0, (intptr_t)n-\u003ename);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1333,"LineContent":"        case NODE_TRAIT: { Trait *pt = last_compiling_trait; last_compiling_trait = (Trait*)1; bc_emit(compiling_bc, OP_TRAIT_DEF, 0, (intptr_t)n-\u003ename); compile_stmt(n-\u003eleft); last_compiling_trait = pt; break; }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1363,"LineContent":"             int idx = bc_emit(compiling_bc, OP_SPAWN, ac, 0);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1421,"LineContent":"                int idx = bc_emit(compiling_bc, OP_SET_PROPERTY, 0, (intptr_t)n-\u003ename);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1590,"LineContent":"                int idx = bc_emit(compiling_bc, OP_GET_PROPERTY, 0, (intptr_t)n-\u003ename);"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1920,"LineContent":"        \u0026\u0026l_OP_FUNC_DEF, \u0026\u0026l_OP_CALL, \u0026\u0026l_OP_CALL_SELF_FAST, \u0026\u0026l_OP_TAIL_CALL, \u0026\u0026l_OP_RETURN,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1937,"LineContent":"        \u0026\u0026l_OP_CLASS_DEF,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1938,"LineContent":"        \u0026\u0026l_OP_METHOD_DEF,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1939,"LineContent":"        \u0026\u0026l_OP_TRAIT_DEF,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1941,"LineContent":"        \u0026\u0026l_OP_GET_PROPERTY,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1942,"LineContent":"        \u0026\u0026l_OP_SET_PROPERTY,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1943,"LineContent":"        \u0026\u0026l_OP_SPAWN,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1946,"LineContent":"        \u0026\u0026l_OP_TRACE_ENTER,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1947,"LineContent":"        \u0026\u0026l_OP_PACKED_ADD,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1953,"LineContent":"        \u0026\u0026l_OP_MATRIX_MUL,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1954,"LineContent":"        \u0026\u0026l_OP_SIMD_ADD,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":1956,"LineContent":"        \u0026\u0026l_OP_ITER_START,"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2196,"LineContent":"    l_OP_CALL_SELF_FAST: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2215,"LineContent":"    l_OP_TAIL_CALL: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2305,"LineContent":"    l_OP_CLASS_DEF: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2310,"LineContent":"    l_OP_METHOD_DEF: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2316,"LineContent":"    l_OP_GET_PROPERTY: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2327,"LineContent":"    l_OP_SET_PROPERTY: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2457,"LineContent":"    l_OP_SPAWN: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2483,"LineContent":"    l_OP_TRAIT_DEF: { const char *n = (const char*)pc-\u003eptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; DISPATCH(); }"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2591,"LineContent":"    l_OP_TRACE_ENTER: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2644,"LineContent":"    l_OP_PACKED_ADD: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2682,"LineContent":"    l_OP_SIMD_ADD: {"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2691,"LineContent":"    l_OP_MATRIX_MUL:"}
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/glowlang.y","LineNumber":2692,"LineContent":"    l_OP_ITER_START: {"}

=== BLOCK ===

Created At: 2026-06-04T14:15:38Z
Completed At: 2026-06-04T14:15:39Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 1 to 80
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: %code requires {
2:     #include "glowlang.h"
3: }
4: 
5: %{
6: #include <stdio.h>
7: #include <stdlib.h>
8: #include <string.h>
9: #include <stdint.h>
10: #include <math.h>
11: #include <ctype.h>
12: #include <time.h>
13: #include <sys/time.h>
14: #include <sys/socket.h>
15: #include <netinet/in.h>
16: #include <arpa/inet.h>
17: #include <netdb.h>
18: #include <errno.h>
19: #include <sys/stat.h>
20: #include <dirent.h>
21: #include <pthread.h>
22: #include <sched.h>
23: #include <sys/mman.h>
24: #include <unistd.h>
25: #include <openssl/ssl.h>
26: #include <openssl/err.h>
27: #include "glowlang.h"
28: #include "lsp.h"
29: #include "jit.h"
30: #include "opt.h"
31: #include <sqlite3.h>
32: 
33: /* VM State */
34: static __thread Value *vm_stack = NULL;
35: static __thread int vm_stack_size = 0;
36: static __thread int sp = 0;
37: static __thread int vm_stack_top = 0; // The highest active register for GC scanning
38: 
39: #define PUSH(v) do { \
40:     if (sp >= vm_stack_size) { \
41:         vm_stack_size = (vm_stack_size == 0) ? 1024 : vm_stack_size * 2; \
42:         vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size); \
43:     } \
44:     vm_stack[sp++] = (v); \
45: } while(0)
46: #define POP() (vm_stack[--sp])
47: 
48: /* Forward declarations */
49: extern __thread int profiling_active;
50: extern __thread int trace_mode;
51: extern const char *intern_string(const char *s);
52: extern void intern_cleanup(void);
53: extern void intern_table_init(void);
54: extern int line_num;
55: extern int yylex(void);
56: extern FILE *yyin;
57: void yyerror(const char *s);
58: 
59: static void gc_mark_value(Value v);
60: static void gc_mark_object(GCObject *obj);
61: #if 0
62: static void gc_collect(void);
63: #endif
64: static void* gc_alloc(size_t sz, ObjType type);
65: void print_value(Value v);
66: static Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg);
67: static void var_set(const char *name, Value value);
68: static Value var_get(const char *name);
69: FuncEntry* func_get(const char *name);
70: static Instance* instance_new(Class *k);
71: static Value class_get_method(Class *k, const char *n);
72: static Value instance_get(Instance *inst, const char *name);
73: static void instance_set(Instance *inst, const char *name, Value val);
74: static Value bound_method_new(Value instance, FuncEntry *method);
75: 
76: Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
77:     (void)argc;
78:     if (pc) call_stack[call_stack_ptr - 1].pc = pc;
79:     if (fe && fe->body) {
80:         TypeSignature sig = compute_signature(args, fe->param_count);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Now let me look at the actual register-based VM in glowlang.y (the real execution engine) and the critical sections:

=== BLOCK ===

Created At: 2026-06-04T14:15:44Z
Completed At: 2026-06-04T14:15:46Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 1900 to 1970
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1900: 
1901:     // Clear locals but preserve parameters and 'self' (which are the first local_count registers)
1902:     int param_count = (bc->owner) ? bc->owner->param_count : 0;
1903:     bool is_action = (bc->owner && bc->owner->local_count > 0 && strcmp(bc->owner->local_names[0], "self") == 0);
1904:     int preserved = is_action ? param_count + 1 : param_count;
1905:     
1906:     for (int i = preserved; i < bc->local_count; i++) {
1907:         vm_stack[base_reg + i] = value_none();
1908:     }
1909: 
1910:     static void* dt[] = {
1911:         &&l_OP_CONST_INT, &&l_OP_CONST_FLOAT, &&l_OP_CONST_STRING, &&l_OP_CONST_BOOL,
1912:         &&l_OP_LOAD_VAR, &&l_OP_STORE_VAR,
1913:         &&l_OP_ADD, &&l_OP_SUB, &&l_OP_MUL, &&l_OP_DIV, &&l_OP_MOD, &&l_OP_POW, &&l_OP_NEG,
1914:         &&l_OP_LT, &&l_OP_GT, &&l_OP_LTE, &&l_OP_GTE, &&l_OP_EQ, &&l_OP_NEQ,
1915:         &&l_OP_AND, &&l_OP_OR, &&l_OP_NOT,
1916:         &&l_OP_JUMP, &&l_OP_JUMP_IF_FALSE, &&l_OP_JUMP_IF_TRUE,
1917:         &&l_OP_LEN, &&l_OP_RANGE, &&l_OP_TYPE, &&l_OP_APPEND, &&l_OP_INPUT, &&l_OP_SHOW, &&l_OP_WRITE,
1918:         &&l_OP_LIST_LITERAL, &&l_OP_DICT_LITERAL,
1919:         &&l_OP_INDEX, &&l_OP_SLICE, &&l_OP_STORE_INDEX,
1920:         &&l_OP_FUNC_DEF, &&l_OP_CALL, &&l_OP_CALL_SELF_FAST, &&l_OP_TAIL_CALL, &&l_OP_RETURN,
1921:         &&l_OP_FOR_EACH, &&l_OP_FOR_EACH_PAIR,
1922:         &&l_OP_STOP, &&l_OP_SKIP,
1923:         &&l_OP_POP,
1924:         &&l_OP_PUSH_SCOPE, &&l_OP_POP_SCOPE, &&l_OP_STORE_LET,
1925:         &&l_OP_LOAD_LOCAL, &&l_OP_STORE_LOCAL,
1926:         &&l_OP_DICT_KEYS, &&l_OP_DICT_VALUES, &&l_OP_DICT_HAS, &&l_OP_DICT_REMOVE, &&l_OP_DICT_MERGE,
1927:         &&l_OP_MATH_ABS, &&l_OP_MATH_MIN, &&l_OP_MATH_MAX, &&l_OP_MATH_ROUND, &&l_OP_MATH_FLOOR, &&l_OP_MATH_CEIL, &&l_OP_MATH_SQRT,
1928:         &&l_OP_STR_UPPER, &&l_OP_STR_LOWER, &&l_OP_STR_TRIM, &&l_OP_STR_FIND, &&l_OP_STR_REPLACE, &&l_OP_STR_STARTSWITH, &&l_OP_STR_ENDSWITH, &&l_OP_STR_SPLIT,
1929:         &&l_OP_LIST_PUSH, &&l_OP_LIST_POP, &&l_OP_LIST_INSERT, &&l_OP_LIST_REMOVE, &&l_OP_LIST_CONTAINS, &&l_OP_LIST_SORT, &&l_OP_LIST_REVERSE,
1930:         &&l_OP_FILE_READ, &&l_OP_FILE_WRITE, &&l_OP_FILE_APPEND,
1931:         &&l_OP_FSTRING,
1932:         &&l_OP_LIST_COMP,
1933:         &&l_OP_THROW,
1934:         &&l_OP_TRY_START, &&l_OP_TRY_END,
1935:         &&l_OP_LAMBDA,
1936:         &&l_OP_CALL_VALUE,
1937:         &&l_OP_CLASS_DEF,
1938:         &&l_OP_METHOD_DEF,
1939:         &&l_OP_TRAIT_DEF,
1940:         &&l_OP_USE_TRAIT,
1941:         &&l_OP_GET_PROPERTY,
1942:         &&l_OP_SET_PROPERTY,
1943:         &&l_OP_SPAWN,
1944:         &&l_OP_MOVE,
1945:         &&l_OP_NOP,
1946:         &&l_OP_TRACE_ENTER,
1947:         &&l_OP_PACKED_ADD,
1948:         &&l_OP_PACKED_SUB,
1949:         &&l_OP_PACKED_MUL,
1950:         &&l_OP_PACKED_LOAD,
1951:         &&l_OP_PACKED_STORE,
1952:         &&l_OP_PACKED_FUSED_MUL_ADD,
1953:         &&l_OP_MATRIX_MUL,
1954:         &&l_OP_SIMD_ADD,
1955:         &&l_OP_USE_FILE,
1956:         &&l_OP_ITER_START,
1957:         &&l_OP_ITER_NEXT
1958:     };
1959: 
1960: #define R(idx) vm_stack[base_reg + (idx)]
1961: #define DISPATCH() { \
1962:     if (profiling_active) pc->exec_count++; \
1963:     if (trace_mode) { \
1964:         printf("DEBUG %04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", (int)(pc - bc->code), op_names[pc->op], pc->r0, pc->r1, pc->r2, pc->arg); \
1965:         visualize_stack(base_reg, bc->local_count); \
1966:     } \
1967:     if (has_exception) goto l_OP_THROW; \
1968:     goto *dt[pc->op]; \
1969: }
1970:     DISPATCH();
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:46Z
Completed At: 2026-06-04T14:15:47Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2580 to 2720
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2580:                     idx++;
2581:                 }
2582:             }
2583:         } else {
2584:             c->captured_names = NULL;
2585:             c->captured_values = NULL;
2586:         }
2587:         R(pc->r0) = value_closure(c);
2588:         pc++; DISPATCH();
2589:     }
2590: 
2591:     l_OP_TRACE_ENTER: {
2592:         Trace *t = (Trace*)pc->trace;
2593:         typedef Value (*TraceFunc)(Value *R, int *pc_off);
2594:         int off = 0;
2595:         Value res = ((TraceFunc)t->jit_code)(&vm_stack[base_reg], &off);
2596:         if (IS_TRACE_EXIT(res)) { pc = bc->code + off; goto *dt[pc->op]; }
2597:         pc->op = pc->original_op; goto *dt[pc->op];
2598:     }
2599:     l_OP_USE_FILE: {
2600:         const char *path = (const char*)pc->ptr;
2601:         FILE *f = fopen(path, "r");
2602:         if (!f) {
2603:             char err[512]; snprintf(err, sizeof(err), "Could not open file: %s", path);
2604:             pending_exception = value_string_copy(err); has_exception = 1; goto l_OP_THROW;
2605:         }
2606:         fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
2607:         if (sz < 0) { fclose(f); pc++; DISPATCH(); }
2608:         char *buf = malloc(sz + 1); 
2609:         size_t nread = fread(buf, 1, sz, f); buf[nread] = '\0'; fclose(f);
2610: 
2611:         Node *old_root = root;
2612:         const char *old_file = compiling_file;
2613:         Bytecode *old_bc = compiling_bc;
2614:         int old_reg = next_free_reg;
2615:         FuncEntry *old_func = compiling_func;
2616:         Class *old_class = last_compiling_class; Trait *old_trait = last_compiling_trait;
2617: 
2618:         compiling_file = path;
2619:         int old_line = line_num; line_num = 1;
2620:         YY_BUFFER_STATE bp = yy_scan_string(buf);
2621:         root = NULL; int parse_res = yyparse(); yy_delete_buffer(bp);
2622:         line_num = old_line;
2623:         if (parse_res != 0) {
2624:             char err[512]; snprintf(err, sizeof(err), "Failed to load module: %s (syntax error)", path);
2625:             pending_exception = value_string_copy(err); has_exception = 1;
2626:             free(buf); goto l_OP_THROW;
2627:         }
2628:         if (root) {
2629:             FuncEntry *fe = arena_alloc(sizeof(FuncEntry)); memset(fe, 0, sizeof(FuncEntry));
2630:             fe->name = "use_file";
2631:             Bytecode *use_bc = bc_new(fe); compiling_bc = use_bc; next_free_reg = 0;
2632:             last_compiling_class = NULL; last_compiling_trait = NULL;
2633:             compiling_func = fe;
2634:             compile(root); bc_emit(use_bc, OP_RETURN, 0, 0);
2635:             run_bytecode(use_bc, NULL, NULL, vm_stack_top);
2636:         }
2637:         root = old_root; compiling_file = old_file; compiling_bc = old_bc; 
2638:         next_free_reg = old_reg; compiling_func = old_func;
2639:         last_compiling_class = old_class; last_compiling_trait = old_trait;
2640:         free(buf); 
2641:         if (has_exception) goto l_OP_THROW;
2642:         pc++; DISPATCH();
2643:     }
2644:     l_OP_PACKED_ADD: {
2645:         Value a = R(pc->r1), b = R(pc->r2);
2646:         if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
2647:             GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
2648:             PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
2649:             if (pa->type == pb->type && pa->size == pb->size) {
2650:                 PackedArray *res = packed_array_new(pa->type, pa->size);
2651:                 if (pa->type == PACKED_FLOAT64) {
2652:                     double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
2653:                     for (int i=0; i<pa->size; i++) dr[i] = da[i] + db[i];
2654:                 } else {
2655:                     int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
2656:                     for (int i=0; i<pa->size; i++) ir[i] = ia[i] + ib[i];
2657:                 }
2658:                 R(pc->r0) = value_packed(res);
2659:             } else R(pc->r0) = value_none();
2660:         } else R(pc->r0) = value_none();
2661:         pc++; DISPATCH();
2662:     }
2663:     l_OP_PACKED_SUB: {
2664:         Value a = R(pc->r1), b = R(pc->r2);
2665:         if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
2666:             GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
2667:             PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
2668:             if (pa->type == pb->type && pa->size == pb->size) {
2669:                 PackedArray *res = packed_array_new(pa->type, pa->size);
2670:                 if (pa->type == PACKED_FLOAT64) {
2671:                     double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
2672:                     for (int i=0; i<pa->size; i++) dr[i] = da[i] - db[i];
2673:                 } else {
2674:                     int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
2675:                     for (int i=0; i<pa->size; i++) ir[i] = ia[i] - ib[i];
2676:                 }
2677:                 R(pc->r0) = value_packed(res);
2678:             } else R(pc->r0) = value_none();
2679:         } else R(pc->r0) = value_none();
2680:         pc++; DISPATCH();
2681:     }
2682:     l_OP_SIMD_ADD: {
2683:         Value a = R(pc->r1), b = R(pc->r2), target = R(pc->r0);
2684:         simd_add_helper(a, b, target);
2685:         pc++; DISPATCH();
2686:     }
2687:     l_OP_PACKED_MUL:
2688:     l_OP_PACKED_LOAD:
2689:     l_OP_PACKED_STORE:
2690:     l_OP_PACKED_FUSED_MUL_ADD:
2691:     l_OP_MATRIX_MUL:
2692:     l_OP_ITER_START: {
2693:         Value coll = R(pc->r1);
2694:         if (GET_TYPE(coll) == VAL_OBJECT && (coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_SQLITE_ITER) {
2695:             R(pc->r0) = coll;
2696:         } else {
2697:             Iterator *it = gc_alloc(sizeof(Iterator), OBJ_ITERATOR);
2698:             it->iterable = coll;
2699:             it->index = 0;
2700:             R(pc->r0) = make_value(VAL_OBJECT, (uintptr_t)it);
2701:         }
2702:         pc++; DISPATCH();
2703:     }
2704:     l_OP_ITER_NEXT: {
2705:         Value vit = R(pc->r1);
2706:         if (GET_TYPE(vit) != VAL_OBJECT || !(vit & MASK_PAYLOAD)) { pc = bc->code + pc->arg; DISPATCH(); }
2707:         GCObject *obj = (GCObject*)(uintptr_t)(vit & MASK_PAYLOAD);
2708:         
2709:         if (obj->type == OBJ_ITERATOR) {
2710:             Iterator *it = (Iterator*)obj;
2711:             Value coll = it->iterable;
2712:             int type = GET_TYPE(coll);
2713:             if (type == VAL_LIST) {
2714:                  List *l = AS_LIST(coll);
2715:                  if (it->index < l->size) {
2716:                      R(pc->r0) = l->items[it->index++];
2717:                      pc++; DISPATCH();
2718:                  }
2719:             } else if (type == VAL_DICT) {
2720:                  Dict *d = AS_DICT(coll);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:47Z
Completed At: 2026-06-04T14:15:48Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 580 to 700
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
580:                 gc_mark_object((GCObject*)rn->internal.right);
581:             }
582:             break;
583:         }
584:         case OBJ_STRING_BUILDER: break; // Buffer is raw malloc'd, no nested objects to mark
585:         default: break;
586:     }
587: }
588: static void gc_mark_value(Value v) {
589:     if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_STRING) {
590:         if (v & MASK_PAYLOAD) gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
591:     }
592: }
593: 
594: static void gc_full_collect(void) {
595:     pthread_mutex_lock(&gc_mutex);
596:     for (int i=0; i<global_table.count; i++) gc_mark_value(global_table.values[i]);
597:     pthread_mutex_lock(&threads_mutex);
598:     for (int i=0; i<active_stack_count; i++) {
599:         Value *stk = *(active_stacks[i].vm_stack_ptr);
600:         int stk_sp = *(active_stacks[i].top_ptr);
601:         if (stk) {
602:             int lim = (stk_sp > 1000000) ? 1000000 : stk_sp;
603:             for (int j=0; j<lim; j++) gc_mark_value(stk[j]);
604:         }
605:         gc_mark_value(*(active_stacks[i].pending_exc_ptr));
606:     }
607:     pthread_mutex_unlock(&threads_mutex);
608:     GCObject **p = &gc_objects;
609:     while (*p) {
610:         if (!(*p)->marked) {
611:             GCObject *unmarked = *p; *p = unmarked->next;
612:             if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
613:             else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
614:             else if (unmarked->type == OBJ_SQLITE_ITER) { if (((SQLiteIter*)unmarked)->stmt) sqlite3_finalize(((SQLiteIter*)unmarked)->stmt); }
615:             else if (unmarked->type == OBJ_SQLITE_HANDLE) { if (((SQLiteHandle*)unmarked)->db) sqlite3_close(((SQLiteHandle*)unmarked)->db); }
616:             else if (unmarked->type == OBJ_INSTANCE) { /* No fields array to free, Dict is handled as OBJ_DICT */ }
617:             else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
618:             else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
619:             else if (unmarked->type == OBJ_PACKED_ARRAY) free(((PackedArray*)unmarked)->data);
620:             else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
621:             gc_allocated -= unmarked->sz;
622:             if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
623:         } else {
624:             (*p)->marked = 0;
625:             if ((*p)->generation == 0) (*p)->generation = 1; // Promote
626:             p = &((*p)->next);
627:         }
628:     }
629:     while (gc_remembered_set) { RememberedSet *next = gc_remembered_set->next; gc_remembered_set->obj->in_remembered_set = 0; free(gc_remembered_set); gc_remembered_set = next; }
630:     pthread_mutex_unlock(&gc_mutex);
631: }
632: 
633: static void gc_minor_collect(void) {
634:     pthread_mutex_lock(&gc_mutex);
635:     for (int i=0; i<global_table.count; i++) gc_mark_value(global_table.values[i]);
636:     pthread_mutex_lock(&threads_mutex);
637:     for (int i=0; i<active_stack_count; i++) {
638:         Value *stk = *(active_stacks[i].vm_stack_ptr);
639:         int stk_sp = *(active_stacks[i].top_ptr);
640:         if (stk) {
641:             int lim = (stk_sp > 1000000) ? 1000000 : stk_sp;
642:             for (int j=0; j<lim; j++) gc_mark_value(stk[j]);
643:         }
644:         gc_mark_value(*(active_stacks[i].pending_exc_ptr));
645:     }
646:     pthread_mutex_unlock(&threads_mutex);
647:     RememberedSet *rs = gc_remembered_set;
648:     while (rs) { gc_mark_object(rs->obj); rs = rs->next; }
649: 
650:     GCObject **p = &gc_objects;
651:     while (*p) {
652:         if ((*p)->generation == 1) { p = &((*p)->next); continue; } 
653:         if (!(*p)->marked) {
654:             GCObject *unmarked = *p; *p = unmarked->next;
655:             if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
656:             else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
657:             else if (unmarked->type == OBJ_SQLITE_ITER) { if (((SQLiteIter*)unmarked)->stmt) sqlite3_finalize(((SQLiteIter*)unmarked)->stmt); }
658:             else if (unmarked->type == OBJ_SQLITE_HANDLE) { if (((SQLiteHandle*)unmarked)->db) sqlite3_close(((SQLiteHandle*)unmarked)->db); }
659:             else if (unmarked->type == OBJ_INSTANCE) { }
660:             else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
661:             else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
662:             else if (unmarked->type == OBJ_PACKED_ARRAY) free(((PackedArray*)unmarked)->data);
663:             else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
664:             gc_allocated -= unmarked->sz;
665:             if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
666:         } else {
667:             (*p)->marked = 0;
668:             p = &((*p)->next);
669:         }
670:     }
671:     pthread_mutex_unlock(&gc_mutex);
672: }
673: 
674: #if 0
675: static void gc_collect(void) { gc_full_collect(); }
676: #endif
677: 
678: static void* gc_alloc(size_t sz, ObjType type) {
679:     if (gc_allocated > gc_threshold) gc_full_collect();
680:     else if (gc_allocated > gc_threshold / 2) gc_minor_collect();
681:     
682:     pthread_mutex_lock(&gc_mutex);
683:     
684:     sz = (sz + 7) & ~7;
685:     GCObject *obj = NULL;
686:     if (sz < gc_block_size / 4) {
687:         if (!gc_current_block || gc_current_block->used + sz > gc_current_block->size) {
688:             gc_current_block = gc_block_new();
689:         }
690:         obj = (GCObject*)(gc_current_block->data + gc_current_block->used);
691:         gc_current_block->used += sz;
692:         gc_current_block->live_count++;
693:         memset(obj, 0, sz);
694:         obj->is_block_allocated = 1;
695:         obj->block_ptr = gc_current_block;
696:     } else {
697:         obj = calloc(1, sz);
698:         obj->is_block_allocated = 0;
699:         obj->block_ptr = NULL;
700:     }
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:48Z
Completed At: 2026-06-04T14:15:49Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 76 to 170
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
76: Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
77:     (void)argc;
78:     if (pc) call_stack[call_stack_ptr - 1].pc = pc;
79:     if (fe && fe->body) {
80:         TypeSignature sig = compute_signature(args, fe->param_count);
81:         void *jit = func_get_specialization(fe, sig);
82:         if (jit && jit != (void*)1) {
83:             if (pc) {
84:                 pc->cache[0].key = (void*)sig;
85:                 pc->cache[0].val = (intptr_t)jit;
86:             }
87:             typedef Value (*JITFunc)(Value *R, int *pc_idx);
88:             int deopt_pc = 0;
89:             call_stack_push(fe, NULL);
90:             Value res = ((JITFunc)jit)(args, &deopt_pc);
91:             call_stack_pop();
92:             return res;
93:         }
94:         return run_bytecode(fe->body, NULL, NULL, (int)(args - vm_stack));
95:     }
96:     return value_none();
97: }
98: 
99: Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc) {
100:     if (pc) call_stack[call_stack_ptr - 1].pc = pc;
101:     FuncEntry *fe = func_get(name);
102:     if (fe) return jit_call_helper(fe, argc, args, pc);
103:     Value v = var_get(name);
104:     return jit_call_helper_val(v, argc, args, pc);
105: }
106: 
107: Value jit_call_helper_val(Value callee, int argc, Value *args, Instruction *pc) {
108:     if (pc) call_stack[call_stack_ptr - 1].pc = pc;
109:     if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
110:         ObjType ot = GET_OBJ_TYPE(callee);
111:         if (ot == OBJ_CLASS) return value_instance(instance_new(AS_CLASS(callee)));
112:         if (ot == OBJ_BOUND_METHOD) {
113:             BoundMethod *bm = AS_BOUND_METHOD(callee);
114:             return jit_call_helper(bm->method, argc, args, pc);
115:         }
116:         if (ot == OBJ_CLOSURE) {
117:             Closure *c = AS_CLOSURE(callee);
118:             if (c->body) return run_bytecode(c->body, NULL, c, (int)(args - vm_stack));
119:         }
120:         if (ot == OBJ_NATIVE_FUNC) {
121:             if (pc && pc->is_method) {
122:                 args++;
123:             }
124:             return AS_NATIVE_FUNC(callee)->func(argc, args);
125:         }
126:     }
127:     return value_none();
128: }
129: 
130: Value packed_array_get_jit(Value v_pa, Value v_idx) {
131:     if (GET_TYPE(v_pa) != VAL_OBJECT || GET_OBJ_TYPE(v_pa) != OBJ_PACKED_ARRAY) return value_none();
132:     PackedArray *pa = AS_PACKED_ARRAY(v_pa);
133:     int idx = (int)val_to_double(v_idx);
134:     if (idx < 0 || idx >= pa->size) return value_none();
135:     if (pa->type == PACKED_INT64) return value_int(((int64_t*)pa->data)[idx]);
136:     return value_float(((double*)pa->data)[idx]);
137: }
138: 
139: void packed_array_set_jit(Value v_pa, Value v_idx, Value v_val) {
140:     if (GET_TYPE(v_pa) != VAL_OBJECT || GET_OBJ_TYPE(v_pa) != OBJ_PACKED_ARRAY) return;
141:     PackedArray *pa = AS_PACKED_ARRAY(v_pa);
142:     int idx = (int)val_to_double(v_idx);
143:     if (idx < 0 || idx >= pa->size) return;
144:     if (pa->type == PACKED_INT64) ((int64_t*)pa->data)[idx] = (int64_t)val_to_double(v_val);
145:     else ((double*)pa->data)[idx] = val_to_double(v_val);
146: }
147: 
148: void simd_add_helper(Value v_a, Value v_b, Value v_target) {
149:     PackedArray *pa = AS_PACKED_ARRAY(v_a), *pb = AS_PACKED_ARRAY(v_b), *pt = AS_PACKED_ARRAY(v_target);
150:     int size = pa->size;
151:     if (pa->type == PACKED_FLOAT64) {
152:         double *da = (double*)pa->data, *db = (double*)pb->data, *dt = (double*)pt->data;
153:         #pragma GCC ivdep
154:         for (int i=0; i<size; i++) dt[i] = da[i] + db[i];
155:     } else {
156:         int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *it = (int64_t*)pt->data;
157:         #pragma GCC ivdep
158:         for (int i=0; i<size; i++) it[i] = ia[i] + ib[i];
159:     }
160: }
161: 
162: double val_to_double(Value v) {
163:     if (((v) & MASK_NAN) != MASK_NAN) return AS_FLOAT(v);
164:     uint64_t type = ((v) & MASK_TYPE) >> 48;
165:     if (type == VAL_INT) return (double)AS_INT(v);
166:     if (type == VAL_BOOL) return (double)AS_BOOL(v);
167:     return 0.0;
168: }
169: 
170: Value value_from_double(double d) {
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Let me read a few more critical sections of the register VM to complete my understanding:

=== BLOCK ===

Created At: 2026-06-04T14:15:55Z
Completed At: 2026-06-04T14:15:57Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2440 to 2600
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2440:     l_OP_STORE_INDEX: {
2441:         Value coll = R(pc->r0), idx = R(pc->r1), val = R(pc->r2);
2442:         if (GET_TYPE(coll) == VAL_LIST) {
2443:             int i = (int)AS_INT(idx); if (i < 0) i += AS_LIST(coll)->size;
2444:             if (i >= 0 && i < AS_LIST(coll)->size) {
2445:                 gc_write_barrier((GCObject*)AS_LIST(coll), val);
2446:                 AS_LIST(coll)->items[i] = val;
2447:             }
2448:         } else if (GET_TYPE(coll) == VAL_OBJECT) {
2449:             if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
2450:                 packed_array_set(AS_PACKED_ARRAY(coll), (int)AS_INT(idx), val);
2451:             }
2452:         } else if (GET_TYPE(coll) == VAL_DICT) {
2453:             dict_set(AS_DICT(coll), AS_STRING(idx), val);
2454:         }
2455:         pc++; DISPATCH();
2456:     }
2457:     l_OP_SPAWN: {
2458:         int ac = pc->arg; Value callee = R(pc->r0);
2459:         ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac;
2460:         td->args = malloc(sizeof(Value)*ac); for(int i=0; i<ac; i++) td->args[i] = R(pc->r1 + i);
2461:         pthread_t t; pthread_create(&t, NULL, thread_wrapper, td); pthread_detach(t); pc++; DISPATCH();
2462:     }
2463:     l_OP_LEN: { Value v = R(pc->r1); if(GET_TYPE(v)==VAL_STRING) R(pc->r0) = value_int(strlen(AS_STRING(v))); else if(GET_TYPE(v)==VAL_LIST) R(pc->r0) = value_int(AS_LIST(v)->size); else R(pc->r0) = value_int(0); pc++; DISPATCH(); }
2464:     l_OP_RANGE: { int n = (int)AS_INT(R(pc->r1)); List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, value_int(i)); R(pc->r0) = value_list(l); pc++; DISPATCH(); }
2465:     l_OP_APPEND: { Value v = R(pc->r1), l = R(pc->r0); if(GET_TYPE(l)==VAL_LIST) list_push(AS_LIST(l), v); pc++; DISPATCH(); }
2466:     l_OP_TYPE: { 
2467:         Value v = R(pc->r1); int t = GET_TYPE(v); 
2468:         if(t == VAL_FLOAT) R(pc->r0) = value_string_copy("float"); 
2469:         else if(t == VAL_INT) R(pc->r0) = value_string_copy("int"); 
2470:         else if(t == VAL_BOOL) R(pc->r0) = value_string_copy("bool"); 
2471:         else if(t == VAL_STRING) R(pc->r0) = value_string_copy("string"); 
2472:         else if(t == VAL_LIST) R(pc->r0) = value_string_copy("list"); 
2473:         else if(t == VAL_DICT) R(pc->r0) = value_string_copy("dict"); 
2474:         else if(t == VAL_OBJECT) R(pc->r0) = value_string_copy("object"); 
2475:         else R(pc->r0) = value_string_copy("none"); 
2476:         pc++; DISPATCH(); 
2477:     }
2478:     l_OP_MOD: R(pc->r0) = value_from_double(fmod(val_to_double(R(pc->r1)), val_to_double(R(pc->r2)))); pc++; DISPATCH();
2479:     l_OP_POW: R(pc->r0) = value_from_double(pow(val_to_double(R(pc->r1)), val_to_double(R(pc->r2)))); pc++; DISPATCH();
2480:     l_OP_NEG: R(pc->r0) = value_from_double(-val_to_double(R(pc->r1))); pc++; DISPATCH();
2481:     l_OP_STOP: goto l_FINISH;
2482:     l_OP_SKIP: pc++; DISPATCH();
2483:     l_OP_TRAIT_DEF: { const char *n = (const char*)pc->ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; DISPATCH(); }
2484:     l_OP_USE_TRAIT: { 
2485:         Value tv = var_get((const char*)pc->ptr); 
2486:         if (GET_TYPE(tv) == VAL_OBJECT && (tv & MASK_PAYLOAD) && GET_OBJ_TYPE(tv) == OBJ_TRAIT && vm_last_class) { 
2487:             Trait *t = AS_TRAIT(tv); 
2488:             for (int i=0; i<t->methods->size; i++) {
2489:                 dict_set(vm_last_class->methods, AS_STRING(t->methods->keys[i]), t->methods->values[i]); 
2490:             }
2491:         } 
2492:         pc++; DISPATCH(); 
2493:     }
2494:     l_OP_INPUT: pc++; DISPATCH();
2495:     l_OP_WRITE: pc++; DISPATCH();
2496:     l_OP_DICT_LITERAL: {
2497:         int n = pc->arg; Dict *d = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Dict), OBJ_DICT) : gc_alloc(sizeof(Dict), OBJ_DICT);
2498:         d->capacity = 8; while(d->capacity < n*2) d->capacity *= 2;
2499:         d->keys = calloc(d->capacity, sizeof(Value));
2500:         d->values = calloc(d->capacity, sizeof(Value));
2501:         for (int i=0; i<d->capacity; i++) d->keys[i] = value_none();
2502:         d->size = 0;
2503:         for (int i=0; i<n; i++) {
2504:             Value k = R(pc->r1 + i * 2);
2505:             Value v = R(pc->r1 + i * 2 + 1);
2506:             if (GET_TYPE(k) == VAL_STRING) {
2507:                 dict_set(d, AS_STRING(k), v);
2508:             }
2509:         }
2510:         R(pc->r0) = value_dict(d); pc++; DISPATCH();
2511:     }
2512:     l_OP_SLICE: pc++; DISPATCH();
2513:     l_OP_FOR_EACH: pc++; DISPATCH();
2514:     l_OP_FOR_EACH_PAIR: pc++; DISPATCH();
2515:     l_OP_PUSH_SCOPE: pc++; DISPATCH();
2516:     l_OP_POP_SCOPE: pc++; DISPATCH();
2517:     l_OP_LOAD_LOCAL: pc++; DISPATCH();
2518:     l_OP_STORE_LOCAL: pc++; DISPATCH();
2519:     l_OP_DICT_KEYS: pc++; DISPATCH();
2520:     l_OP_DICT_VALUES: pc++; DISPATCH();
2521:     l_OP_DICT_HAS: pc++; DISPATCH();
2522:     l_OP_DICT_REMOVE: pc++; DISPATCH();
2523:     l_OP_DICT_MERGE: pc++; DISPATCH();
2524:     l_OP_MATH_ABS: pc++; DISPATCH();
2525:     l_OP_MATH_MIN: pc++; DISPATCH();
2526:     l_OP_MATH_MAX: pc++; DISPATCH();
2527:     l_OP_MATH_ROUND: pc++; DISPATCH();
2528:     l_OP_MATH_FLOOR: pc++; DISPATCH();
2529:     l_OP_MATH_CEIL: pc++; DISPATCH();
2530:     l_OP_MATH_SQRT: pc++; DISPATCH();
2531:     l_OP_STR_UPPER: pc++; DISPATCH();
2532:     l_OP_STR_LOWER: pc++; DISPATCH();
2533:     l_OP_STR_TRIM: pc++; DISPATCH();
2534:     l_OP_STR_FIND: pc++; DISPATCH();
2535:     l_OP_STR_REPLACE: pc++; DISPATCH();
2536:     l_OP_STR_STARTSWITH: pc++; DISPATCH();
2537:     l_OP_STR_ENDSWITH: pc++; DISPATCH();
2538:     l_OP_STR_SPLIT: pc++; DISPATCH();
2539:     l_OP_LIST_PUSH: pc++; DISPATCH();
2540:     l_OP_LIST_POP: pc++; DISPATCH();
2541:     l_OP_LIST_INSERT: pc++; DISPATCH();
2542:     l_OP_LIST_REMOVE: pc++; DISPATCH();
2543:     l_OP_LIST_CONTAINS: pc++; DISPATCH();
2544:     l_OP_LIST_SORT: pc++; DISPATCH();
2545:     l_OP_LIST_REVERSE: pc++; DISPATCH();
2546:     l_OP_FILE_READ: pc++; DISPATCH();
2547:     l_OP_FILE_WRITE: pc++; DISPATCH();
2548:     l_OP_FILE_APPEND: pc++; DISPATCH();
2549:     l_OP_FSTRING: pc++; DISPATCH();
2550:     l_OP_LIST_COMP: pc++; DISPATCH();
2551:     l_OP_LAMBDA: {
2552:         FuncEntry *fe = func_get((const char*)pc->ptr);
2553:         Closure *c = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Closure), OBJ_CLOSURE) : gc_alloc(sizeof(Closure), OBJ_CLOSURE);
2554:         c->param_count = fe->param_count; c->params = fe->params; c->body = fe->body;
2555:         FuncEntry *parent_fe = (FuncEntry*)pc->cache[0].key;
2556:         int capture_size = 0;
2557:         if (parent_fe) capture_size += parent_fe->param_count + parent_fe->local_count;
2558:         if (current_closure) capture_size += current_closure->captured_count;
2559:         c->captured_count = capture_size;
2560:         if (capture_size > 0) {
2561:             c->captured_names = malloc(sizeof(const char*) * capture_size);
2562:             c->captured_values = malloc(sizeof(Value) * capture_size);
2563:             int idx = 0;
2564:             if (current_closure) {
2565:                 for (int i=0; i<current_closure->captured_count; i++) {
2566:                     c->captured_names[idx] = current_closure->captured_names[i];
2567:                     c->captured_values[idx] = current_closure->captured_values[i];
2568:                     idx++;
2569:                 }
2570:             }
2571:             if (parent_fe) {
2572:                 for (int i=0; i<parent_fe->param_count; i++) {
2573:                     c->captured_names[idx] = parent_fe->params[i];
2574:                     c->captured_values[idx] = vm_stack[base_reg + i];
2575:                     idx++;
2576:                 }
2577:                 for (int i=0; i<parent_fe->local_count; i++) {
2578:                     c->captured_names[idx] = parent_fe->local_names[i];
2579:                     c->captured_values[idx] = vm_stack[base_reg + parent_fe->param_count + i];
2580:                     idx++;
2581:                 }
2582:             }
2583:         } else {
2584:             c->captured_names = NULL;
2585:             c->captured_values = NULL;
2586:         }
2587:         R(pc->r0) = value_closure(c);
2588:         pc++; DISPATCH();
2589:     }
2590: 
2591:     l_OP_TRACE_ENTER: {
2592:         Trace *t = (Trace*)pc->trace;
2593:         typedef Value (*TraceFunc)(Value *R, int *pc_off);
2594:         int off = 0;
2595:         Value res = ((TraceFunc)t->jit_code)(&vm_stack[base_reg], &off);
2596:         if (IS_TRACE_EXIT(res)) { pc = bc->code + off; goto *dt[pc->op]; }
2597:         pc->op = pc->original_op; goto *dt[pc->op];
2598:     }
2599:     l_OP_USE_FILE: {
2600:         const char *path = (const char*)pc->ptr;
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:57Z
Completed At: 2026-06-04T14:15:58Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2190 to 2320
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2190:                 has_exception = 1;
2191:                 goto l_OP_THROW;
2192:             }
2193:         }
2194:         pc++; DISPATCH();
2195:     }
2196:     l_OP_CALL_SELF_FAST: {
2197:         // int ac = pc->arg; 
2198:         FuncEntry *fe = bc->owner;
2199:         if (fe && fe->body) {
2200:             TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
2201:             void *jit = func_get_specialization(fe, sig);
2202:             if (jit && jit != (void*)1) {
2203:                 typedef Value (*JITFunc)(Value *R, int *pc_idx);
2204:                 JITFunc f = (JITFunc)jit; int deopt_pc = 0;
2205:                 Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
2206:                 if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
2207:                 if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2208:             } else {
2209:                 Value res = run_bytecode(fe->body, NULL, NULL, base_reg + pc->r2);
2210:                 if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2211:             }
2212:         }
2213:         pc++; DISPATCH();
2214:     }
2215:     l_OP_TAIL_CALL: {
2216:         int ac = pc->arg; const char *n = (const char*)pc->ptr;
2217:         FuncEntry *fe = NULL;
2218:         Value callee = 0;
2219:         if (n) fe = func_get(n);
2220:         else {
2221:             callee = R(pc->r1);
2222:             if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
2223:                 ObjType ot = GET_OBJ_TYPE(callee);
2224:                 if (ot == OBJ_BOUND_METHOD) fe = AS_BOUND_METHOD(callee)->method;
2225:                 else if (ot == OBJ_NATIVE_FUNC) {
2226:                     Value *args = &R(pc->r2);
2227:                     int argc = ac;
2228:                     if (pc->is_method) {
2229:                         args++;
2230:                     }
2231:                     R(pc->r0) = AS_NATIVE_FUNC(callee)->func(argc, args);
2232:                     goto l_FINISH;
2233:                 }
2234:             }
2235:         }
2236:         
2237:         if (fe && fe->body) {
2238:             int needed = fe->param_count;
2239:             if (fe->body->local_count > needed) needed = fe->body->local_count;
2240:             if (base_reg + needed >= vm_stack_size) {
2241:                 int old_size = vm_stack_size;
2242:                 while (base_reg + needed >= vm_stack_size) vm_stack_size *= 2;
2243:                 vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
2244:                 for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
2245:             }
2246:             call_stack[my_frame_idx].fe = fe;
2247:             call_stack[my_frame_idx].pc = fe->body->code;
2248:             int arg_base = base_reg + pc->r2;
2249:             if (arg_base + ac >= vm_stack_size) {
2250:                  goto l_OP_CALL; 
2251:             }
2252:             for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i] = vm_stack[arg_base + i];
2253:             for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i] = value_none();
2254:             bc = fe->body; pc = bc->code;
2255:             stack_arena_used = old_arena_used;
2256:             if (bc->jit_code) {
2257:                 typedef Value (*JITFunc)(Value *R, int *pc_idx);
2258:                 int deopt_pc = 0;
2259:                 Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
2260:                 if (IS_DEOPT(res)) { pc = &bc->code[deopt_pc]; goto l_RESUME_INTERP; }
2261:                 R(0) = res; goto l_FINISH;
2262:             }
2263:             goto l_RESUME_INTERP;
2264:         }
2265:         goto l_OP_CALL;
2266:     }
2267:     l_OP_RETURN: {
2268:         R(0) = R(pc->r0);
2269:         goto l_FINISH;
2270:     }
2271:     l_OP_POP: pc++; DISPATCH();
2272:     l_OP_SHOW: { int n = pc->arg; int base = pc->r0; for (int i=0; i<n; i++) { print_value(R(base+i)); if(i<n-1) printf(" "); } printf("\n"); pc++; DISPATCH(); }
2273:     l_OP_STORE_LET: {
2274:         int slot = pc->arg;
2275:         global_table.values[slot] = R(pc->r0);
2276:         pc++; DISPATCH();
2277:     }
2278:     l_OP_TRY_START: pc++; DISPATCH();
2279:     l_OP_TRY_END: pc++; DISPATCH();
2280:     l_OP_THROW: {
2281:         Value exc = (has_exception) ? pending_exception : R(pc->r0);
2282:         pending_exception = exc; has_exception = 1;
2283:         call_stack[my_frame_idx].pc = pc; // Record error location
2284:         
2285:         if (!exception_stack_trace) {
2286:             exception_stack_trace = format_stack_trace();
2287:             error_file = pc->file;
2288:             error_line = pc->line;
2289:         }
2290: 
2291:         int current_pc_idx = (int)(pc - bc->code);
2292:         for (int i = 0; i < bc->exception_count; i++) {
2293:             if (current_pc_idx >= bc->exception_table[i].start_pc && 
2294:                 current_pc_idx < bc->exception_table[i].end_pc) {
2295:                 R(0) = exc;
2296:                 has_exception = 0;
2297:                 pending_exception = 0;
2298:                 if (exception_stack_trace) { free(exception_stack_trace); exception_stack_trace = NULL; }
2299:                 pc = bc->code + bc->exception_table[i].handler_pc;
2300:                 DISPATCH();
2301:             }
2302:         }
2303:         goto l_FINISH;
2304:     }
2305:     l_OP_CLASS_DEF: {
2306:         const char *n = (const char*)pc->ptr; Class *p = NULL;
2307:         if (pc->arg == 1) { Value pv = R(pc->r1); if (GET_TYPE(pv) == VAL_OBJECT && (pv & MASK_PAYLOAD)) p = AS_CLASS(pv); }
2308:         Value c = value_class(class_new(n, p)); var_set(n, c); vm_last_class = AS_CLASS(c); pc++; DISPATCH();
2309:     }
2310:     l_OP_METHOD_DEF: {
2311:         FuncEntry *fe = func_get((const char*)pc->ptr);
2312:         if(vm_last_class) dict_set(vm_last_class->methods, fe->name, bound_method_new(value_none(), fe));
2313:         else if(vm_last_trait) dict_set(vm_last_trait->methods, fe->name, bound_method_new(value_none(), fe));
2314:         pc++; DISPATCH();
2315:     }
2316:     l_OP_GET_PROPERTY: {
2317:         const char *n = (const char*)pc->ptr; Value o = R(pc->r1);
2318:         printf("DEBUG INTERP GET_PROPERTY: prop=%s, obj_reg=%d, obj_val=0x%lx, obj_type=%d\n", n, pc->r1, (unsigned long)o, (int)GET_TYPE(o));
2319:         if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
2320:             ObjType ot = GET_OBJ_TYPE(o);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:58Z
Completed At: 2026-06-04T14:15:59Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2690 to 2780
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2690:     l_OP_PACKED_FUSED_MUL_ADD:
2691:     l_OP_MATRIX_MUL:
2692:     l_OP_ITER_START: {
2693:         Value coll = R(pc->r1);
2694:         if (GET_TYPE(coll) == VAL_OBJECT && (coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_SQLITE_ITER) {
2695:             R(pc->r0) = coll;
2696:         } else {
2697:             Iterator *it = gc_alloc(sizeof(Iterator), OBJ_ITERATOR);
2698:             it->iterable = coll;
2699:             it->index = 0;
2700:             R(pc->r0) = make_value(VAL_OBJECT, (uintptr_t)it);
2701:         }
2702:         pc++; DISPATCH();
2703:     }
2704:     l_OP_ITER_NEXT: {
2705:         Value vit = R(pc->r1);
2706:         if (GET_TYPE(vit) != VAL_OBJECT || !(vit & MASK_PAYLOAD)) { pc = bc->code + pc->arg; DISPATCH(); }
2707:         GCObject *obj = (GCObject*)(uintptr_t)(vit & MASK_PAYLOAD);
2708:         
2709:         if (obj->type == OBJ_ITERATOR) {
2710:             Iterator *it = (Iterator*)obj;
2711:             Value coll = it->iterable;
2712:             int type = GET_TYPE(coll);
2713:             if (type == VAL_LIST) {
2714:                  List *l = AS_LIST(coll);
2715:                  if (it->index < l->size) {
2716:                      R(pc->r0) = l->items[it->index++];
2717:                      pc++; DISPATCH();
2718:                  }
2719:             } else if (type == VAL_DICT) {
2720:                  Dict *d = AS_DICT(coll);
2721:                  while (it->index < d->capacity && (d->keys[it->index] == 0 || GET_TYPE(d->keys[it->index]) == VAL_NONE)) it->index++;
2722:                  if (it->index < d->capacity) {
2723:                      R(pc->r0) = d->keys[it->index++];
2724:                      pc++; DISPATCH();
2725:                  }
2726:             }
2727:         } else if (obj->type == OBJ_SQLITE_ITER) {
2728:             SQLiteIter *it = (SQLiteIter*)obj;
2729:             if (!it->done) {
2730:                 int rc = sqlite3_step(it->stmt);
2731:                 if (rc == SQLITE_ROW) {
2732:                     int cols = sqlite3_column_count(it->stmt);
2733:                     Dict *row = dict_new(cols);
2734:                     for (int i = 0; i < cols; i++) {
2735:                         const char *name = sqlite3_column_name(it->stmt, i);
2736:                         int type = sqlite3_column_type(it->stmt, i);
2737:                         Value v;
2738:                         if (type == SQLITE_INTEGER) v = value_int(sqlite3_column_int64(it->stmt, i));
2739:                         else if (type == SQLITE_FLOAT) v = value_float(sqlite3_column_double(it->stmt, i));
2740:                         else if (type == SQLITE_TEXT) v = value_string_copy((const char*)sqlite3_column_text(it->stmt, i));
2741:                         else v = value_none();
2742:                         dict_set(row, name, v);
2743:                     }
2744:                     R(pc->r0) = value_dict(row);
2745:                     pc++; DISPATCH();
2746:                 } else {
2747:                     it->done = 1;
2748:                     sqlite3_finalize(it->stmt);
2749:                     it->stmt = NULL;
2750:                 }
2751:             }
2752:         }
2753:         
2754:         pc = bc->code + pc->arg; DISPATCH();
2755:     }
2756:     l_OP_NOP: pc++; DISPATCH();
2757: 
2758: l_FINISH:
2759:     if (has_exception && call_depth == 1) {
2760:         print_error_trace(pending_exception);
2761:         has_exception = 0; // Prevent double report
2762:     }
2763:     call_stack_pop();
2764:     vm_stack_top = old_top;
2765:     stack_arena_used = old_arena_used;
2766:     call_depth--;
2767:     return (has_exception) ? value_none() : R(0);
2768: }
2769: 
2770: void print_value(Value v) {
2771:     if (v == 0) { printf("none"); return; }
2772:     if ((v & MASK_NAN) != MASK_NAN) { printf("%g", AS_FLOAT(v)); return; }
2773:     uint64_t type = (v & MASK_TYPE) >> 48;
2774:     if (type == VAL_INT) printf("%ld", (long)AS_INT(v));
2775:     else if (type == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
2776:     else if (type == VAL_STRING) printf("%s", AS_STRING(v));
2777:     else if (type == VAL_LIST) { List *l = AS_LIST(v); printf("["); for (int i=0; i<l->size; i++) { print_value(l->items[i]); if (i < l->size - 1) printf(", "); } printf("]"); }
2778:     else if (type == VAL_OBJECT) {
2779:         if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
2780:         ObjType ot = GET_OBJ_TYPE(v);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Created At: 2026-06-04T14:15:59Z
Completed At: 2026-06-04T14:15:59Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 1820 to 1900
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1820:     "CONST_INT", "CONST_FLOAT", "CONST_STRING", "CONST_BOOL", "LOAD_VAR", "STORE_VAR",
1821:     "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "NEG", "LT", "GT", "LTE", "GTE", "EQ", "NEQ",
1822:     "AND", "OR", "NOT", "JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "LEN", "RANGE", "TYPE",
1823:     "APPEND", "INPUT", "SHOW", "WRITE", "LIST_LITERAL", "DICT_LITERAL", "INDEX", "SLICE",
1824:     "STORE_INDEX", "FUNC_DEF", "CALL", "CALL_SELF_FAST", "TAIL_CALL", "RETURN",
1825:     "FOR_EACH", "FOR_EACH_PAIR", "STOP", "SKIP", "POP", "PUSH_SCOPE", "POP_SCOPE",
1826:     "STORE_LET", "LOAD_LOCAL", "STORE_LOCAL", "DICT_KEYS", "DICT_VALUES", "DICT_HAS",
1827:     "DICT_REMOVE", "DICT_MERGE", "MATH_ABS", "MATH_MIN", "MATH_MAX", "MATH_ROUND",
1828:     "MATH_FLOOR", "MATH_CEIL", "MATH_SQRT", "STR_UPPER", "STR_LOWER", "STR_TRIM",
1829:     "STR_FIND", "STR_REPLACE", "STR_STARTSWITH", "STR_ENDSWITH", "STR_SPLIT",
1830:     "LIST_PUSH", "LIST_POP", "LIST_INSERT", "LIST_REMOVE", "LIST_CONTAINS", "LIST_SORT",
1831:     "LIST_REVERSE", "FILE_READ", "FILE_WRITE", "FILE_APPEND", "FSTRING", "LIST_COMP",
1832:     "THROW", "TRY_START", "TRY_END", "LAMBDA", "CALL_VALUE", "CLASS_DEF", "METHOD_DEF",
1833:     "TRAIT_DEF", "USE_TRAIT", "GET_PROPERTY", "SET_PROPERTY", "SPAWN", "MOVE", "NOP",
1834:     "TRACE_ENTER", "PACKED_ADD", "PACKED_SUB", "PACKED_MUL", "PACKED_LOAD", "PACKED_STORE",
1835:     "PACKED_FUSED_MUL_ADD", "MATRIX_MUL", "SIMD_ADD", "USE_FILE", "ITER_START", "ITER_NEXT"
1836: };
1837: 
1838: static void print_bytecode(Bytecode *bc) {
1839:     if (!bc) return;
1840:     for (int i = 0; i < bc->count; i++) {
1841:         Instruction *pc = &bc->code[i];
1842:         const char *name = (pc->op < sizeof(op_names)/sizeof(op_names[0])) ? op_names[pc->op] : "UNKNOWN";
1843:         printf("%04d: %-15s r0:%d r1:%d r2:%d arg:%d ptr:%p\n", i, name, pc->r0, pc->r1, pc->r2, pc->arg, (void*)pc->ptr);
1844:     }
1845: }
1846: 
1847: static Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg) {
1848:     static __thread int call_depth = 0;
1849:     if (++call_depth > 1000) {
1850:         pending_exception = value_string_copy("Maximum recursion depth exceeded");
1851:         has_exception = 1;
1852:         call_depth--;
1853:         return value_none();
1854:     }
1855: 
1856:     call_stack_push(bc->owner, start_pc ? start_pc : bc->code);
1857:     int my_frame_idx = call_stack_ptr - 1;
1858: 
1859:     size_t old_arena_used = stack_arena_used;
1860:     int old_top = vm_stack_top;
1861:     
1862:     // Ensure the entire call frame and its incoming arguments are rooted for the GC
1863:     if (base_reg + bc->local_count + 16 > vm_stack_top) {
1864:         vm_stack_top = base_reg + bc->local_count + 16;
1865:     }
1866: 
1867:     // static __thread long total_instructions = 0;
1868:     // static __thread Trace *recording_trace = NULL;
1869: 
1870:     Instruction *pc = start_pc ? start_pc : bc->code;
1871: 
1872:     if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
1873:         bc->code[0].exec_count++;
1874:         if (0) jit_compile(bc);
1875:     }
1876:     if (!start_pc && bc->jit_code) {
1877:         typedef Value (*JITFunc)(Value *R, int *pc_idx);
1878:         int deopt_pc = 0;
1879:         Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
1880:         if (IS_DEOPT(res)) {
1881:             pc = &bc->code[deopt_pc];
1882:             goto l_RESUME_INTERP;
1883:         }
1884:         call_stack_pop();
1885:         vm_stack_top = old_top;
1886:         stack_arena_used = old_arena_used;
1887:         call_depth--;
1888:         return res;
1889:     }
1890: 
1891:     l_RESUME_INTERP:;
1892:     if (base_reg + bc->local_count >= vm_stack_size) {
1893:         int old_size = vm_stack_size;
1894:         vm_stack_size = (vm_stack_size == 0) ? 4096 : vm_stack_size * 2;
1895:         while (base_reg + bc->local_count >= vm_stack_size) vm_stack_size *= 2;
1896:         vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
1897:         for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
1898:     }
1899:     if (base_reg + bc->local_count > vm_stack_top) vm_stack_top = base_reg + bc->local_count;
1900: 
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Now I have a comprehensive understanding. Let me also check the `ir_build_from_ast` function to see if it's a stub:

=== BLOCK ===

Created At: 2026-06-04T14:16:05Z
Completed At: 2026-06-04T14:16:07Z
{"File":"/home/anshumali/Desktop/GlowLang 1.0/src/ir.h","LineNumber":72,"LineContent":"void ir_build_from_ast(IRFunction *f, struct Node *n);"}

=== BLOCK ===

Created At: 2026-06-04T14:16:07Z
Completed At: 2026-06-04T14:16:07Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2320 to 2455
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2320:             ObjType ot = GET_OBJ_TYPE(o);
2321:             if(ot==OBJ_INSTANCE) {
2322:                 R(pc->r0) = instance_get(AS_INSTANCE(o), n);
2323:             } else R(pc->r0) = value_none();
2324:         } else if(GET_TYPE(o)==VAL_DICT) R(pc->r0) = dict_get(AS_DICT(o), n);
2325:         else R(pc->r0) = value_none(); pc++; DISPATCH();
2326:     }
2327:     l_OP_SET_PROPERTY: {
2328:         const char *n = (const char*)pc->ptr; Value o = R(pc->r0); Value v = R(pc->r1);
2329:         if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
2330:             instance_set(AS_INSTANCE(o), n, v);
2331:         }
2332:         else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
2333:         pc++; DISPATCH();
2334:     }
2335:     l_OP_CALL_VALUE: {
2336:         int ac = pc->arg; Value callee = R(pc->r1);
2337:         call_stack[my_frame_idx].pc = pc; // Update current frame PC
2338:         if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
2339:             ObjType ot = GET_OBJ_TYPE(callee);
2340:             if(ot==OBJ_CLASS) {
2341:                 Value inst = value_instance(instance_new(AS_CLASS(callee)));
2342:                 R(pc->r0) = inst;
2343:                 Value init_method = class_get_method(AS_CLASS(callee), "init");
2344:                 if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
2345:                     Value temp[256];
2346:                     int count = (ac > 255) ? 255 : ac;
2347:                     for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);
2348: 
2349:                     int callee_base = base_reg + pc->r2;
2350:                     if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) {
2351:                         int old_size = vm_stack_size;
2352:                         while (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
2353:                         vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
2354:                         for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
2355:                     }
2356:                     vm_stack[callee_base] = inst; // self at R0
2357:                     for (int i=0; i<count; i++) vm_stack[callee_base + i + 1] = temp[i];
2358: 
2359:                     Value old_self = var_get("self");
2360:                     var_set("self", inst);
2361:                     FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
2362:                     if (ife && ife->body) {
2363:                         run_bytecode(ife->body, NULL, NULL, callee_base);
2364:                     }
2365:                     var_set("self", old_self);
2366:                 }
2367:             }
2368:             else if(ot==OBJ_BOUND_METHOD) {
2369:                 BoundMethod *bm = AS_BOUND_METHOD(callee);
2370:                 if(bm->method && bm->method->body) {
2371:                     int callee_base = base_reg + pc->r2;
2372:                     if (callee_base + bm->method->body->local_count + 16 >= vm_stack_size) {
2373:                         int old_size = vm_stack_size;
2374:                         while (callee_base + bm->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
2375:                         vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
2376:                         for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
2377:                     }
2378:                     if (GET_TYPE(bm->instance) != VAL_NONE) {
2379:                         vm_stack[callee_base] = bm->instance; // self at R0
2380:                     }
2381:                     Value res = run_bytecode(bm->method->body, NULL, NULL, callee_base);
2382:                     if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2383:                 } else R(pc->r0) = value_none();
2384:             } else if(ot==OBJ_CLOSURE) {
2385:                 Closure *c = AS_CLOSURE(callee);
2386:                 if(c->body) {
2387:                     Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);
2388:                     if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2389:                 } else R(pc->r0) = value_none();
2390:             } else if (ot == OBJ_NATIVE_FUNC) {
2391:                 Value *args = &R(pc->r2);
2392:                 int argc = ac;
2393:                 if (pc->is_method) {
2394:                     args++;
2395:                 }
2396:                 R(pc->r0) = AS_NATIVE_FUNC(callee)->func(argc, args);
2397:             } else {
2398:                 pending_exception = value_string_copy("Object is not callable");
2399:                 has_exception = 1;
2400:                 goto l_OP_THROW;
2401:             }
2402:         } else {
2403:             pending_exception = value_string_copy("Attempted to call a non-object value");
2404:             has_exception = 1;
2405:             goto l_OP_THROW;
2406:         }
2407:         pc++; DISPATCH();
2408:     }
2409:     l_OP_LIST_LITERAL: {
2410:         int n = pc->arg; List *l = pc->is_stack_alloc ? gc_alloc_stack(sizeof(List), OBJ_LIST) : gc_alloc(sizeof(List), OBJ_LIST);
2411:         l->size = 0; l->capacity = n; l->items = n > 0 ? calloc(n, sizeof(Value)) : NULL;
2412:         for (int i=0; i<n; i++) list_push(l, R(pc->r1 + i));
2413:         R(pc->r0) = value_list(l); pc++; DISPATCH();
2414:     }
2415:     l_OP_INDEX: {
2416:         Value coll = R(pc->r1), idx_arg = R(pc->r2);
2417:         if (GET_TYPE(coll) == VAL_LIST) {
2418:             int i = (int)AS_INT(idx_arg); if (i < 0) i += AS_LIST(coll)->size;
2419:             if (i >= 0 && i < AS_LIST(coll)->size) R(pc->r0) = AS_LIST(coll)->items[i];
2420:             else R(pc->r0) = value_none();
2421:         } else if (GET_TYPE(coll) == VAL_OBJECT) {
2422:             if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
2423:                 R(pc->r0) = packed_array_get(AS_PACKED_ARRAY(coll), (int)AS_INT(idx_arg));
2424:             } else R(pc->r0) = value_none();
2425:         } else if (GET_TYPE(coll) == VAL_DICT) {
2426:             Value v = dict_get(AS_DICT(coll), AS_STRING(idx_arg));
2427:             // If we're accessing a function in a dictionary, treat it as a bound method
2428:             // so that l_OP_CALL_VALUE handles arguments correctly.
2429:             if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
2430:                 ObjType ot = GET_OBJ_TYPE(v);
2431:                 if (ot == OBJ_NATIVE_FUNC || ot == OBJ_BOUND_METHOD) {
2432:                     // For dicts, we don't bind 'self' unless it's explicitly done.
2433:                     // But we must return the callable.
2434:                 }
2435:             }
2436:             R(pc->r0) = v;
2437:         } else R(pc->r0) = value_none();
2438:         pc++; DISPATCH();
2439:     }
2440:     l_OP_STORE_INDEX: {
2441:         Value coll = R(pc->r0), idx = R(pc->r1), val = R(pc->r2);
2442:         if (GET_TYPE(coll) == VAL_LIST) {
2443:             int i = (int)AS_INT(idx); if (i < 0) i += AS_LIST(coll)->size;
2444:             if (i >= 0 && i < AS_LIST(coll)->size) {
2445:                 gc_write_barrier((GCObject*)AS_LIST(coll), val);
2446:                 AS_LIST(coll)->items[i] = val;
2447:             }
2448:         } else if (GET_TYPE(coll) == VAL_OBJECT) {
2449:             if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
2450:                 packed_array_set(AS_PACKED_ARRAY(coll), (int)AS_INT(idx), val);
2451:             }
2452:         } else if (GET_TYPE(coll) == VAL_DICT) {
2453:             dict_set(AS_DICT(coll), AS_STRING(idx), val);
2454:         }
2455:         pc++; DISPATCH();
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Now I have a thorough understanding of the entire codebase. Let me write the comprehensive audit report.

=== BLOCK ===

Created At: 2026-06-04T14:16:12Z
Completed At: 2026-06-04T14:16:12Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 1870 to 1874
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1870:     Instruction *pc = start_pc ? start_pc : bc->code;
1871: 
1872:     if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
1873:         bc->code[0].exec_count++;
1874:         if (0) jit_compile(bc);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

This is critical - `if (0) jit_compile(bc)` means auto-JIT is **dead code**. Now let me produce the full audit report.

=== BLOCK ===

Created At: 2026-06-04T14:16:16Z
Completed At: 2026-06-04T14:16:17Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 2100 to 2200
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
2100:                 if (IS_DEOPT(res)) {
2101:                     res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
2102:                 }
2103:                 if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2104:                 pc++; DISPATCH();
2105:             }
2106: 
2107:             void *jit = func_get_specialization(fe, sig);
2108:             if (jit && jit != (void*)1) {
2109:                 // Update Cache
2110:                 pc->cache[0].key = (void*)sig;
2111:                 pc->cache[0].val = (intptr_t)jit;
2112: 
2113:                 typedef Value (*JITFunc)(Value *R, int *pc_idx);
2114:                 JITFunc f = (JITFunc)jit;
2115:                 int deopt_pc = 0;
2116:                 Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
2117:                 if (IS_DEOPT(res)) {
2118:                     res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
2119:                 }
2120:                 if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2121:             } else {
2122:                 fe->used++;
2123:                 if (fe->used > 10 && fe->spec_count < 8) {
2124:                     void *jit_check = func_get_specialization(fe, sig);
2125:                     if (!jit_check) {
2126:                         jit_compile_specialized(fe, sig);
2127:                         if (!func_get_specialization(fe, sig)) {
2128:                             func_add_specialization(fe, sig, (void*)1, 0);
2129:                         }
2130:                     }
2131:                 }
2132:                 Value res = run_bytecode(fe->body, NULL, NULL, base_reg + pc->r2);
2133:                 if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2134:             }
2135:         } else {
2136:             Value v = var_get(n);
2137:             if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
2138:                 ObjType ot = GET_OBJ_TYPE(v);
2139:                 if (ot == OBJ_CLASS) {
2140:                     Value inst = value_instance(instance_new(AS_CLASS(v)));
2141:                     R(pc->r0) = inst;
2142:                     Value init_method = class_get_method(AS_CLASS(v), "init");
2143:                     if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
2144:                         Value temp[256];
2145:                         int count = (ac > 255) ? 255 : ac;
2146:                         for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);
2147:                         
2148:                         int callee_base = base_reg + pc->r2;
2149:                         if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) {
2150:                             int old_size = vm_stack_size;
2151:                             while (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
2152:                             vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
2153:                             for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
2154:                         }
2155:                         
2156:                         vm_stack[callee_base] = inst; // self
2157:                         for (int i=0; i<count; i++) vm_stack[callee_base + i + 1] = temp[i];
2158: 
2159:                         Value old_self = var_get("self");
2160:                         var_set("self", inst);
2161:                         FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
2162:                         if (ife && ife->body) {
2163:                             run_bytecode(ife->body, NULL, NULL, callee_base);
2164:                         }
2165:                         var_set("self", old_self);
2166:                     }
2167:                 }
2168:                 else if (ot == OBJ_BOUND_METHOD) {
2169:                     BoundMethod *bm = AS_BOUND_METHOD(v);
2170:                     if (bm->method && bm->method->body) {
2171:                         Value old_self = var_get("self");
2172:                         var_set("self", bm->instance);
2173:                         Value res = run_bytecode(bm->method->body, NULL, NULL, base_reg + pc->r2);
2174:                         var_set("self", old_self);
2175:                         if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
2176:                     }
2177:                 }
2178:                 else if (ot == OBJ_NATIVE_FUNC) {
2179:                     Value *args = &R(pc->r2);
2180:                     int argc = ac;
2181:                     if (pc->is_method) {
2182:                         args++;
2183:                     }
2184:                     R(pc->r0) = AS_NATIVE_FUNC(v)->func(argc, args);
2185:                 }
2186:                 else R(pc->r0) = value_none();
2187:             } else {
2188:                 char msg[256]; snprintf(msg, 256, "Function '%s' not found", n);
2189:                 pending_exception = value_string_copy(msg);
2190:                 has_exception = 1;
2191:                 goto l_OP_THROW;
2192:             }
2193:         }
2194:         pc++; DISPATCH();
2195:     }
2196:     l_OP_CALL_SELF_FAST: {
2197:         // int ac = pc->arg; 
2198:         FuncEntry *fe = bc->owner;
2199:         if (fe && fe->body) {
2200:             TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Now I have all the data I need. Let me write the comprehensive audit report:

=== BLOCK ===

Created At: 2026-06-04T14:16:21Z
Completed At: 2026-06-04T14:16:22Z
File Path: `file:///home/anshumali/Desktop/GlowLang%201.0/src/glowlang.y`
Total Lines: 4146
Total Bytes: 183474
Showing lines 1970 to 2080
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1970:     DISPATCH();
1971: 
1972:     l_OP_CONST_INT: R(pc->r0) = value_int(pc->arg); pc++; DISPATCH();
1973:     l_OP_CONST_FLOAT: R(pc->r0) = *(Value*)pc->ptr; pc++; DISPATCH();
1974:     l_OP_CONST_STRING: R(pc->r0) = value_string_copy((const char*)pc->ptr); pc++; DISPATCH();
1975:     l_OP_CONST_BOOL: R(pc->r0) = value_bool(pc->arg); pc++; DISPATCH();
1976:     l_OP_LOAD_VAR: {
1977:         int slot = pc->arg;
1978:         // printf("LOAD_VAR: slot=%d, count=%d, values=%p\n", slot, global_table.count, global_table.values);
1979:         if (slot >= global_table.count) { fprintf(stderr, "ERROR: LOAD_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
1980:         R(pc->r0) = global_table.values[slot];
1981:         pc++; DISPATCH();
1982:     }
1983:     l_OP_STORE_VAR: {
1984:         int slot = pc->arg;
1985:         // printf("STORE_VAR: slot=%d, count=%d, values=%p\n", slot, global_table.count, global_table.values);
1986:         if (slot >= global_table.count) { fprintf(stderr, "ERROR: STORE_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
1987:         global_table.values[slot] = R(pc->r0);
1988:         pc++; DISPATCH();
1989:     }
1990:     l_OP_MOVE: R(pc->r0) = R(pc->r1); pc++; DISPATCH();
1991:     l_OP_ADD: {
1992:         Value a = R(pc->r1), b = R(pc->r2), res;
1993:         if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
1994:             pc->type = 1;
1995:             res = value_int(AS_INT(a) + AS_INT(b));
1996:         } else if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
1997:             Value sa, sb;
1998:             if (GET_TYPE(a) == VAL_STRING) sa = a;
1999:             else { char buf[64]; if(GET_TYPE(a)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(a)); else if(GET_TYPE(a)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(a)); else strcpy(buf, ""); sa = value_string_copy(buf); }
2000:             if (GET_TYPE(b) == VAL_STRING) sb = b;
2001:             else { char buf[64]; if(GET_TYPE(b)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(b)); else if(GET_TYPE(b)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(b)); else strcpy(buf, ""); sb = value_string_copy(buf); }
2002:             res = rope_new(sa, sb);
2003:         } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
2004:             pc->type = 5;
2005:             List *la = AS_LIST(a), *lb = AS_LIST(b);
2006:             List *rl = list_new(la->size + lb->size);
2007:             for (int i=0; i<la->size; i++) list_push(rl, la->items[i]);
2008:             for (int i=0; i<lb->size; i++) list_push(rl, lb->items[i]);
2009:             res = value_list(rl);
2010:         } else {
2011:             if (GET_TYPE(a) == VAL_FLOAT || GET_TYPE(b) == VAL_FLOAT) pc->type = 2;
2012:             res = value_from_double(val_to_double(a) + val_to_double(b));
2013:         }
2014:         R(pc->r0) = res; pc++; DISPATCH();
2015:     }
2016:     l_OP_SUB: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_int(AS_INT(a) - AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_from_double(val_to_double(a) - val_to_double(b)); } pc++; DISPATCH(); }
2017:     l_OP_MUL: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_int(AS_INT(a) * AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_from_double(val_to_double(a) * val_to_double(b)); } pc++; DISPATCH(); }
2018:     l_OP_DIV: { 
2019:         Value a = R(pc->r1), b = R(pc->r2);
2020:         if (val_to_double(b) == 0.0) {
2021:             pending_exception = value_string_copy("Division by zero");
2022:             has_exception = 1;
2023:             goto l_OP_THROW;
2024:         }
2025:         if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
2026:             pc->type = 1;
2027:             R(pc->r0) = value_int(AS_INT(a) / AS_INT(b));
2028:         } else {
2029:             pc->type = 2;
2030:             R(pc->r0) = value_from_double(val_to_double(a) / val_to_double(b));
2031:         }
2032:         pc++; DISPATCH(); 
2033:     }
2034:     l_OP_LT: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) < AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) < val_to_double(b)); } pc++; DISPATCH(); }
2035:     l_OP_GT: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) > AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) > val_to_double(b)); } pc++; DISPATCH(); }
2036:     l_OP_LTE: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) <= AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) <= val_to_double(b)); } pc++; DISPATCH(); }
2037:     l_OP_GTE: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) >= AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) >= val_to_double(b)); } pc++; DISPATCH(); }
2038:     l_OP_EQ: { pc->type=1; R(pc->r0) = value_bool(val_eq(R(pc->r1), R(pc->r2))); pc++; DISPATCH(); }
2039:     l_OP_NEQ: R(pc->r0) = value_bool(!val_eq(R(pc->r1), R(pc->r2))); pc++; DISPATCH();
2040:     l_OP_AND: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) && value_truthy(R(pc->r2))); pc++; DISPATCH();
2041:     l_OP_OR: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) || value_truthy(R(pc->r2))); pc++; DISPATCH();
2042:     l_OP_NOT: R(pc->r0) = value_bool(!value_truthy(R(pc->r1))); pc++; DISPATCH();
2043:     l_OP_JUMP: {
2044:         if (pc->arg < (pc - bc->code)) {
2045:             pc->exec_count++;
2046:             /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
2047:                 recording_trace = malloc(sizeof(Trace));
2048:                 recording_trace->instructions = malloc(sizeof(Instruction) * 128);
2049:                 recording_trace->original_offsets = malloc(sizeof(int) * 128);
2050:                 recording_trace->count = 0; recording_trace->capacity = 128;
2051:                 recording_trace->start_pc = pc;
2052:             } */
2053:         }
2054:         pc = bc->code + pc->arg; DISPATCH();
2055:     }
2056:     l_OP_JUMP_IF_FALSE: {
2057:         if (pc->arg < (pc - bc->code) && !value_truthy(R(pc->r0))) {
2058:             pc->exec_count++;
2059:             /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
2060:                 recording_trace = malloc(sizeof(Trace));
2061:                 recording_trace->instructions = malloc(sizeof(Instruction) * 128);
2062:                 recording_trace->original_offsets = malloc(sizeof(int) * 128);
2063:                 recording_trace->count = 0; recording_trace->capacity = 128;
2064:                 recording_trace->start_pc = pc;
2065:             } */
2066:         }
2067:         if(!value_truthy(R(pc->r0))) pc = bc->code + pc->arg; else pc++; DISPATCH();
2068:     }
2069:     l_OP_JUMP_IF_TRUE: {
2070:         if (pc->arg < (pc - bc->code) && value_truthy(R(pc->r0))) {
2071:             pc->exec_count++;
2072:             /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
2073:                 recording_trace = malloc(sizeof(Trace));
2074:                 recording_trace->instructions = malloc(sizeof(Instruction) * 128);
2075:                 recording_trace->original_offsets = malloc(sizeof(int) * 128);
2076:                 recording_trace->count = 0; recording_trace->capacity = 128;
2077:                 recording_trace->start_pc = pc;
2078:             } */
2079:         }
2080:         if(value_truthy(R(pc->r0))) pc = bc->code + pc->arg; else pc++; DISPATCH();
The above content does NOT show the entire file contents. If you need to view any lines of the file which were not shown to complete your task, call this tool again to view those lines.


=== BLOCK ===

Now I have complete data. Let me write the comprehensive audit report and send it to the main agent.

=== BLOCK ===

Created At: 2026-06-04T14:16:30Z
Completed At: 2026-06-04T14:16:30Z
Message sent to "ef5470f7-dd74-46f7-9d83-ea6a94952c9a".