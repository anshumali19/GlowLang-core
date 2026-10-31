/* Core library: everyday built-in functions and the methods of lists,
   strings and dicts.
 *
 * Methods are found through glow_builtin_method(), which the interpreter's
 * method calls, property reads and the JIT's call helper all use, so every
 * execution path offers the same methods. A method receives its receiver in
 * args[0] and its arguments after it.
 */
#include "glowlang.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int glow_value_cmp(Value a, Value b);

/* ---- helpers ---- */

static Value raise(const char *msg) {
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

static int is_num(Value v) {
    ValueType t = GET_TYPE(v);
    return t == VAL_INT || t == VAL_FLOAT || (t == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BIGINT);
}

static int arg_is(int argc, Value *args, int i, ValueType t) {
    return i < argc && GET_TYPE(args[i]) == t && (t != VAL_STRING || (args[i] & MASK_PAYLOAD));
}

/* Call a GlowLang function value with the given arguments. */
static Value call_value(Value fn, int argc, Value *args) {
    Value r = jit_call_helper_val(fn, argc, args, NULL);
    if (IS_DEOPT(r)) return raise("Expected a function");
    return r;
}

static Value list_value_copy(List *src) {
    List *l = list_new(src->size > 0 ? src->size : 1);
    Value v = value_list(l);
    for (int i = 0; i < src->size; i++) list_push(l, src->items[i]);
    return v;
}

/* Stable merge sort of items[0..n) by glow_value_cmp, or by key values. */
static void merge_sort(Value *items, Value *keys, Value *tmp_i, Value *tmp_k, int n) {
    if (n < 2) return;
    int mid = n / 2;
    merge_sort(items, keys, tmp_i, tmp_k, mid);
    merge_sort(items + mid, keys + mid, tmp_i, tmp_k, n - mid);
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (glow_value_cmp(keys[j], keys[i]) < 0) { tmp_i[k] = items[j]; tmp_k[k++] = keys[j++]; }
        else { tmp_i[k] = items[i]; tmp_k[k++] = keys[i++]; }
    }
    while (i < mid) { tmp_i[k] = items[i]; tmp_k[k++] = keys[i++]; }
    while (j < n) { tmp_i[k] = items[j]; tmp_k[k++] = keys[j++]; }
    memcpy(items, tmp_i, sizeof(Value) * n);
    memcpy(keys, tmp_k, sizeof(Value) * n);
}

/* Sort a list in place; key_fn (optional) maps each item to its sort key. */
static int sort_list(List *l, Value key_fn, int descending) {
    int n = l->size;
    if (n < 2) return 1;
    Value *keys = malloc(sizeof(Value) * n), *ti = malloc(sizeof(Value) * n), *tk = malloc(sizeof(Value) * n);
    /* Keys computed by GlowLang code are kept in a list so the collector sees them. */
    List *key_list = list_new(n);
    Value key_list_v = value_list(key_list);
    for (int i = 0; i < n; i++) {
        Value k = l->items[i];
        if (GET_TYPE(key_fn) != VAL_NONE) {
            k = call_value(key_fn, 1, &l->items[i]);
            if (has_exception) { free(keys); free(ti); free(tk); return 0; }
        }
        list_push(key_list, k);
    }
    memcpy(keys, key_list->items, sizeof(Value) * n);
    merge_sort(l->items, keys, ti, tk, n);
    if (descending) {
        for (int i = 0, j = n - 1; i < j; i++, j--) { Value t = l->items[i]; l->items[i] = l->items[j]; l->items[j] = t; }
    }
    (void)key_list_v;
    free(keys); free(ti); free(tk);
    return 1;
}

/* Append the text of v to a growing buffer. */
static void buf_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        while (*len + n + 1 > *cap) *cap = *cap ? *cap * 2 : 64;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static Value join_values(List *l, const char *sep) {
    char *buf = NULL; size_t len = 0, cap = 0;
    size_t seplen = strlen(sep);
    buf_append(&buf, &len, &cap, "", 0);
    for (int i = 0; i < l->size; i++) {
        if (i) buf_append(&buf, &len, &cap, sep, seplen);
        Value s = value_to_string_value(l->items[i]);
        buf_append(&buf, &len, &cap, AS_STRING(s), rope_length(s));
    }
    Value r = value_string_copy(buf);
    free(buf);
    return r;
}

/* ---- global functions ---- */

static Value core_abs(int argc, Value *args) {
    if (argc < 1 || !is_num(args[0])) return raise("abs() needs a number");
    Value v = args[0];
    if (GET_TYPE(v) == VAL_INT) { int64_t x = AS_INT(v); return x < 0 ? value_from_double(-(double)x) : v; }
    return value_from_double(fabs(val_to_double(v)));
}

/* min/max of the arguments, or of a single list argument. */
static Value extreme(int argc, Value *args, int want, const char *name) {
    Value *items = args; int n = argc;
    if (argc == 1 && GET_TYPE(args[0]) == VAL_LIST) { items = AS_LIST(args[0])->items; n = AS_LIST(args[0])->size; }
    if (n == 0) { char m[64]; snprintf(m, sizeof(m), "%s() of an empty list", name); return raise(m); }
    Value best = items[0];
    for (int i = 1; i < n; i++) if (glow_value_cmp(items[i], best) * want > 0) best = items[i];
    return best;
}
static Value core_min(int argc, Value *args) { return extreme(argc, args, -1, "min"); }
static Value core_max(int argc, Value *args) { return extreme(argc, args, 1, "max"); }

static Value core_sum(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_LIST) return raise("sum() needs a list");
    List *l = AS_LIST(args[0]);
    int all_int = 1; int64_t isum = 0; double fsum = 0;
    for (int i = 0; i < l->size; i++) {
        Value v = l->items[i];
        if (!is_num(v)) return raise("sum() needs a list of numbers");
        if (all_int && GET_TYPE(v) == VAL_INT) {
            int64_t x = AS_INT(v);
            if ((x > 0 && isum > INT64_MAX / 2) || (x < 0 && isum < INT64_MIN / 2)) all_int = 0;
            else { isum += x; continue; }
        }
        if (all_int) { fsum = (double)isum; all_int = 0; }
        fsum += val_to_double(v);
    }
    if (all_int) {
        if (isum >= -9007199254740992LL && isum <= 9007199254740992LL) return value_from_double((double)isum);
        return value_bigint(bigint_from_int64(isum));
    }
    return value_from_double(fsum);
}

/* sorted(list, key, descending): a new sorted list. */
static Value core_sorted(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_LIST) return raise("sorted() needs a list");
    Value copy = list_value_copy(AS_LIST(args[0]));
    Value key = argc >= 2 ? args[1] : value_none();
    int desc = argc >= 3 && value_truthy(args[2]);
    if (!sort_list(AS_LIST(copy), key, desc)) return value_none();
    return copy;
}

static Value core_reversed(int argc, Value *args) {
    if (argc < 1) return raise("reversed() needs a list or string");
    if (GET_TYPE(args[0]) == VAL_STRING) {
        size_t n = rope_length(args[0]);
        char *buf = malloc(n + 1);
        const char *s = AS_STRING(args[0]);
        for (size_t i = 0; i < n; i++) buf[i] = s[n - 1 - i];
        buf[n] = '\0';
        Value r = value_string_copy(buf);
        free(buf);
        return r;
    }
    if (GET_TYPE(args[0]) != VAL_LIST) return raise("reversed() needs a list or string");
    List *src = AS_LIST(args[0]);
    List *l = list_new(src->size > 0 ? src->size : 1);
    Value v = value_list(l);
    for (int i = src->size - 1; i >= 0; i--) list_push(l, src->items[i]);
    return v;
}

/* chr(code): the character with that Unicode code point, as UTF-8 text
   (chr(27) is the escape character terminals use for colors). */
static Value core_chr(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return raise("chr() needs a whole number");
    int64_t c = AS_INT(args[0]);
    if (c < 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return raise("chr(): not a Unicode code point");
    char b[5] = { 0 };
    if (c < 0x80) b[0] = (char)c;
    else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); }
    else { b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 0x3F)); b[2] = (char)(0x80 | ((c >> 6) & 0x3F)); b[3] = (char)(0x80 | (c & 0x3F)); }
    if (c == 0) return value_string_copy("");   /* text cannot hold a NUL */
    return value_string_copy(b);
}

/* ord(text): the code point of the first character of text (UTF-8). */
static Value core_ord(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return raise("ord() needs a string");
    const unsigned char *s = (const unsigned char*)AS_STRING(args[0]);
    if (!s[0]) return raise("ord() of an empty string");
    int n = s[0] < 0x80 ? 1 : (s[0] >> 5) == 6 ? 2 : (s[0] >> 4) == 14 ? 3 : (s[0] >> 3) == 30 ? 4 : 0;
    if (!n) return value_int(s[0]);
    int64_t c = n == 1 ? s[0] : n == 2 ? (s[0] & 0x1F) : n == 3 ? (s[0] & 0x0F) : (s[0] & 0x07);
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return value_int(s[0]);   /* not valid UTF-8: the byte */
        c = (c << 6) | (s[i] & 0x3F);
    }
    return value_int(c);
}

static Value core_float(int argc, Value *args) {
    if (argc < 1) return value_float(0.0);
    Value v = args[0];
    if (is_num(v)) return value_float(val_to_double(v));
    if (GET_TYPE(v) == VAL_BOOL) return value_float(AS_BOOL(v) ? 1.0 : 0.0);
    if (GET_TYPE(v) == VAL_STRING) {
        const char *s = AS_STRING(v);
        char *end;
        double d = strtod(s, &end);
        while (*end && isspace((unsigned char)*end)) end++;
        if (end == s || *end) { char m[160]; snprintf(m, sizeof(m), "Cannot convert \"%.100s\" to a number", s); return raise(m); }
        return value_float(d);
    }
    return raise("float() needs a number or a string");
}

static Value core_bool(int argc, Value *args) {
    if (argc < 1) return value_bool(0);
    Value v = args[0];
    if (GET_TYPE(v) == VAL_STRING) return value_bool(rope_length(v) > 0);
    if (GET_TYPE(v) == VAL_LIST) return value_bool(AS_LIST(v)->size > 0);
    if (GET_TYPE(v) == VAL_DICT) return value_bool(AS_DICT(v)->size > 0);
    return value_bool(value_truthy(v));
}

/* ---- string methods ---- */

static Value str_map(Value s, int (*f)(int)) {
    size_t n = rope_length(s);
    char *buf = malloc(n + 1);
    const char *src = AS_STRING(s);
    for (size_t i = 0; i < n; i++) buf[i] = (char)f((unsigned char)src[i]);
    buf[n] = '\0';
    Value r = value_string_copy(buf);
    free(buf);
    return r;
}
static Value s_upper(int argc, Value *args) { (void)argc; return str_map(args[0], toupper); }
static Value s_lower(int argc, Value *args) { (void)argc; return str_map(args[0], tolower); }

static Value trim_impl(Value s, int left, int right) {
    const char *p = AS_STRING(s);
    size_t n = rope_length(s), a = 0, b = n;
    if (left) while (a < b && isspace((unsigned char)p[a])) a++;
    if (right) while (b > a && isspace((unsigned char)p[b - 1])) b--;
    char *buf = malloc(b - a + 1);
    memcpy(buf, p + a, b - a);
    buf[b - a] = '\0';
    Value r = value_string_copy(buf);
    free(buf);
    return r;
}
static Value s_trim(int argc, Value *args) { (void)argc; return trim_impl(args[0], 1, 1); }
static Value s_trim_start(int argc, Value *args) { (void)argc; return trim_impl(args[0], 1, 0); }
static Value s_trim_end(int argc, Value *args) { (void)argc; return trim_impl(args[0], 0, 1); }

static Value s_starts_with(int argc, Value *args) {
    if (!arg_is(argc, args, 1, VAL_STRING)) return raise("starts_with() needs a string");
    size_t n = rope_length(args[0]), m = rope_length(args[1]);
    return value_bool(m <= n && memcmp(AS_STRING(args[0]), AS_STRING(args[1]), m) == 0);
}
static Value s_ends_with(int argc, Value *args) {
    if (!arg_is(argc, args, 1, VAL_STRING)) return raise("ends_with() needs a string");
    size_t n = rope_length(args[0]), m = rope_length(args[1]);
    return value_bool(m <= n && memcmp(AS_STRING(args[0]) + (n - m), AS_STRING(args[1]), m) == 0);
}

/* text.replace(old, new): every occurrence. */
static Value s_replace(int argc, Value *args) {
    if (!arg_is(argc, args, 1, VAL_STRING) || !arg_is(argc, args, 2, VAL_STRING)) return raise("replace() needs two strings");
    const char *s = AS_STRING(args[0]);
    size_t n = rope_length(args[0]);
    const char *from = AS_STRING(args[1]); size_t fl = rope_length(args[1]);
    const char *to = AS_STRING(args[2]); size_t tl = rope_length(args[2]);
    if (fl == 0) return args[0];
    char *buf = NULL; size_t len = 0, cap = 0;
    buf_append(&buf, &len, &cap, "", 0);
    size_t i = 0;
    while (i < n) {
        if (i + fl <= n && memcmp(s + i, from, fl) == 0) { buf_append(&buf, &len, &cap, to, tl); i += fl; }
        else { buf_append(&buf, &len, &cap, s + i, 1); i++; }
    }
    Value r = value_string_copy(buf);
    free(buf);
    return r;
}

static Value s_count(int argc, Value *args) {
    if (!arg_is(argc, args, 1, VAL_STRING)) return raise("count() needs a string");
    const char *s = AS_STRING(args[0]); size_t n = rope_length(args[0]);
    const char *sub = AS_STRING(args[1]); size_t m = rope_length(args[1]);
    if (m == 0) return value_from_double(0);
    int64_t c = 0;
    for (size_t i = 0; i + m <= n; ) { if (memcmp(s + i, sub, m) == 0) { c++; i += m; } else i++; }
    return value_from_double((double)c);
}

/* separator.join(list) */
static Value s_join(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_LIST) return raise("join() needs a list");
    return join_values(AS_LIST(args[1]), AS_STRING(args[0]));
}

static Value s_lines(int argc, Value *args) {
    (void)argc;
    Value nl = value_string_copy("\n");
    Value split_args[2] = { args[0], nl };
    Value parts = native_str_split(2, split_args);
    List *l = AS_LIST(parts);
    for (int i = 0; i < l->size; i++) {
        Value p = l->items[i];
        size_t n = rope_length(p);
        if (n && AS_STRING(p)[n - 1] == '\r') l->items[i] = trim_impl(p, 0, 1);
    }
    if (l->size > 0 && rope_length(l->items[l->size - 1]) == 0) l->size--;
    return parts;
}

static Value s_is_number(int argc, Value *args) {
    (void)argc;
    const char *s = AS_STRING(args[0]);
    char *end;
    strtod(s, &end);
    while (*end && isspace((unsigned char)*end)) end++;
    return value_bool(end != s && *end == '\0');
}

/* ---- list methods ---- */

static Value l_insert(int argc, Value *args) {
    if (argc < 3 || GET_TYPE(args[1]) != VAL_INT) return raise("insert() needs an index and a value");
    List *l = AS_LIST(args[0]);
    int64_t i = AS_INT(args[1]);
    if (i < 0) i += l->size;
    if (i < 0) i = 0;
    if (i > l->size) i = l->size;
    list_push(l, value_none());
    memmove(&l->items[i + 1], &l->items[i], sizeof(Value) * (size_t)(l->size - 1 - i));
    l->items[i] = args[2];
    gc_write_barrier((GCObject*)l, args[2]);
    return value_none();
}

static int list_find(List *l, Value v) {
    for (int i = 0; i < l->size; i++) if (val_eq(l->items[i], v)) return i;
    return -1;
}

/* Remove the first item equal to v; true when one was removed. */
static Value l_remove(int argc, Value *args) {
    if (argc < 2) return raise("remove() needs a value");
    List *l = AS_LIST(args[0]);
    int i = list_find(l, args[1]);
    if (i < 0) return value_bool(0);
    memmove(&l->items[i], &l->items[i + 1], sizeof(Value) * (size_t)(l->size - 1 - i));
    l->size--;
    return value_bool(1);
}

/* Position of the first item equal to v, or -1. */
static Value l_index(int argc, Value *args) {
    if (argc < 2) return raise("index() needs a value");
    return value_from_double((double)list_find(AS_LIST(args[0]), args[1]));
}
static Value l_contains(int argc, Value *args) {
    if (argc < 2) return raise("contains() needs a value");
    return value_bool(list_find(AS_LIST(args[0]), args[1]) >= 0);
}
static Value l_join(int argc, Value *args) {
    const char *sep = arg_is(argc, args, 1, VAL_STRING) ? AS_STRING(args[1]) : "";
    return join_values(AS_LIST(args[0]), sep);
}
/* list.sort(key, descending): sorts in place, returns the list. */
static Value l_sort(int argc, Value *args) {
    Value key = argc >= 2 ? args[1] : value_none();
    int desc = argc >= 3 && value_truthy(args[2]);
    if (!sort_list(AS_LIST(args[0]), key, desc)) return value_none();
    return args[0];
}
static Value l_reverse(int argc, Value *args) {
    (void)argc;
    List *l = AS_LIST(args[0]);
    for (int i = 0, j = l->size - 1; i < j; i++, j--) { Value t = l->items[i]; l->items[i] = l->items[j]; l->items[j] = t; }
    return args[0];
}
static Value l_extend(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_LIST) return raise("extend() needs a list");
    List *l = AS_LIST(args[0]), *src = AS_LIST(args[1]);
    int n = src->size;
    for (int i = 0; i < n; i++) { list_push(l, src->items[i]); gc_write_barrier((GCObject*)l, src->items[i]); }
    return args[0];
}
static Value l_copy(int argc, Value *args) { (void)argc; return list_value_copy(AS_LIST(args[0])); }
static Value l_clear(int argc, Value *args) { (void)argc; AS_LIST(args[0])->size = 0; return args[0]; }
static Value l_first(int argc, Value *args) {
    (void)argc; List *l = AS_LIST(args[0]);
    return l->size ? l->items[0] : raise("first() of an empty list");
}
static Value l_last(int argc, Value *args) {
    (void)argc; List *l = AS_LIST(args[0]);
    return l->size ? l->items[l->size - 1] : raise("last() of an empty list");
}
static Value l_is_empty(int argc, Value *args) { (void)argc; return value_bool(AS_LIST(args[0])->size == 0); }

/* list.map(fn), list.filter(fn), list.each(fn), list.reduce(fn, start) */
static Value l_map(int argc, Value *args) {
    if (argc < 2) return raise("map() needs a function");
    List *src = AS_LIST(args[0]);
    List *out = list_new(src->size > 0 ? src->size : 1);
    Value out_v = value_list(out);
    for (int i = 0; i < src->size; i++) {
        Value r = call_value(args[1], 1, &src->items[i]);
        if (has_exception) return value_none();
        list_push(out, r);
    }
    return out_v;
}
static Value l_filter(int argc, Value *args) {
    if (argc < 2) return raise("filter() needs a function");
    List *src = AS_LIST(args[0]);
    List *out = list_new(4);
    Value out_v = value_list(out);
    for (int i = 0; i < src->size; i++) {
        Value item = src->items[i];
        Value r = call_value(args[1], 1, &item);
        if (has_exception) return value_none();
        if (value_truthy(r)) list_push(out, item);
    }
    return out_v;
}
static Value l_each(int argc, Value *args) {
    if (argc < 2) return raise("each() needs a function");
    List *src = AS_LIST(args[0]);
    for (int i = 0; i < src->size; i++) {
        call_value(args[1], 1, &src->items[i]);
        if (has_exception) return value_none();
    }
    return args[0];
}
static Value l_reduce(int argc, Value *args) {
    if (argc < 2) return raise("reduce() needs a function");
    List *src = AS_LIST(args[0]);
    int i = 0;
    Value acc;
    if (argc >= 3) acc = args[2];
    else if (src->size) acc = src->items[i++];
    else return raise("reduce() of an empty list with no start value");
    for (; i < src->size; i++) {
        Value pair[2] = { acc, src->items[i] };
        acc = call_value(args[1], 2, pair);
        if (has_exception) return value_none();
    }
    return acc;
}

/* ---- dict methods ---- */

/* dict.get(key, default) */
static Value d_get(int argc, Value *args) {
    if (!arg_is(argc, args, 1, VAL_STRING)) return raise("get() needs a string key");
    Dict *d = AS_DICT(args[0]);
    int i = dict_get_idx(d, AS_STRING(args[1]));
    if (i >= 0) return d->values[i];
    return argc >= 3 ? args[2] : value_none();
}
static Value d_items(int argc, Value *args) {
    extern Value glow_native_pairs(int argc, Value *args);
    (void)argc;
    return glow_native_pairs(1, args);
}
static Value d_copy(int argc, Value *args) {
    (void)argc;
    Dict *src = AS_DICT(args[0]);
    Value out = value_dict(dict_new(src->size > 4 ? src->size * 2 : 8));
    for (int i = 0; i < src->capacity; i++) {
        if (GET_TYPE(src->keys[i]) == VAL_NONE) continue;
        dict_set(AS_DICT(out), (const char*)(uintptr_t)(src->keys[i] & MASK_PAYLOAD), src->values[i]);
    }
    return out;
}
static Value d_is_empty(int argc, Value *args) { (void)argc; return value_bool(AS_DICT(args[0])->size == 0); }

/* ---- method table ---- */

typedef struct { const char *name; NativeFuncPtr fn; } MethodEntry;

static const MethodEntry string_methods[] = {
    {"upper", s_upper}, {"lower", s_lower}, {"trim", s_trim}, {"strip", s_trim},
    {"trim_start", s_trim_start}, {"trim_end", s_trim_end},
    {"starts_with", s_starts_with}, {"ends_with", s_ends_with}, {"replace", s_replace},
    {"count", s_count}, {"join", s_join}, {"lines", s_lines}, {"is_number", s_is_number},
    {"contains", native_str_contains}, {"find", native_str_find}, {"slice", native_str_slice},
    {"split", native_str_split}, {"len", native_len}, {NULL, NULL}
};
static const MethodEntry list_methods[] = {
    {"push", native_list_append}, {"append", native_list_append}, {"add", native_list_append},
    {"pop", native_list_pop}, {"len", native_list_len},
    {"insert", l_insert}, {"remove", l_remove}, {"index", l_index}, {"contains", l_contains},
    {"join", l_join}, {"sort", l_sort}, {"reverse", l_reverse}, {"extend", l_extend},
    {"copy", l_copy}, {"clear", l_clear}, {"first", l_first}, {"last", l_last},
    {"is_empty", l_is_empty}, {"map", l_map}, {"filter", l_filter}, {"each", l_each},
    {"reduce", l_reduce}, {NULL, NULL}
};
static const MethodEntry dict_methods[] = {
    {"has", native_dict_has}, {"keys", native_dict_keys}, {"values", native_dict_values},
    {"remove", native_dict_remove}, {"merge", native_dict_merge}, {"len", native_len},
    {"get", d_get}, {"items", d_items}, {"copy", d_copy}, {"is_empty", d_is_empty}, {NULL, NULL}
};

static NativeFuncPtr find_method(const MethodEntry *t, const char *name) {
    for (; t->name; t++) if (strcmp(t->name, name) == 0) return t->fn;
    return NULL;
}

/* The built-in method `name` of v, or NULL. For a dict, a callable stored
   under that key (module functions such as fs.remove) takes precedence. */
NativeFuncPtr glow_builtin_method(Value v, const char *name) {
    if (!name) return NULL;
    switch (GET_TYPE(v)) {
        case VAL_STRING: return (v & MASK_PAYLOAD) ? find_method(string_methods, name) : NULL;
        case VAL_LIST: return find_method(list_methods, name);
        case VAL_DICT: {
            Value m = dict_get(AS_DICT(v), name);
            if (GET_TYPE(m) == VAL_OBJECT && (m & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(m);
                if (ot == OBJ_NATIVE_FUNC || ot == OBJ_CLOSURE || ot == OBJ_BOUND_METHOD || ot == OBJ_CLASS) return NULL;
            }
            return find_method(dict_methods, name);
        }
        default: return NULL;
    }
}

/* ---- math ---- */

static Value m_exp(int argc, Value *args) { return argc && is_num(args[0]) ? value_from_double(exp(val_to_double(args[0]))) : raise("exp() needs a number"); }
static Value m_log(int argc, Value *args) {
    if (!argc || !is_num(args[0])) return raise("log() needs a number");
    double x = val_to_double(args[0]);
    if (x <= 0) return raise("log() of a number that is not positive");
    if (argc >= 2 && is_num(args[1])) return value_from_double(log(x) / log(val_to_double(args[1])));
    return value_from_double(log(x));
}
static Value m_log10(int argc, Value *args) {
    if (!argc || !is_num(args[0]) || val_to_double(args[0]) <= 0) return raise("log10() needs a positive number");
    return value_from_double(log10(val_to_double(args[0])));
}
static Value m_tan(int argc, Value *args) { return argc && is_num(args[0]) ? value_from_double(tan(val_to_double(args[0]))) : raise("tan() needs a number"); }
static Value m_asin(int argc, Value *args) { return argc && is_num(args[0]) ? value_from_double(asin(val_to_double(args[0]))) : raise("asin() needs a number"); }
static Value m_acos(int argc, Value *args) { return argc && is_num(args[0]) ? value_from_double(acos(val_to_double(args[0]))) : raise("acos() needs a number"); }
static Value m_atan(int argc, Value *args) { return argc && is_num(args[0]) ? value_from_double(atan(val_to_double(args[0]))) : raise("atan() needs a number"); }
static Value m_atan2(int argc, Value *args) {
    return argc >= 2 && is_num(args[0]) && is_num(args[1]) ? value_from_double(atan2(val_to_double(args[0]), val_to_double(args[1]))) : raise("atan2() needs two numbers");
}
static Value m_hypot(int argc, Value *args) {
    return argc >= 2 && is_num(args[0]) && is_num(args[1]) ? value_from_double(hypot(val_to_double(args[0]), val_to_double(args[1]))) : raise("hypot() needs two numbers");
}

/* Register the global functions and extend the math/os/debug modules. */
void glow_corelib_init(void) {
    var_set("abs", native_func_new("abs", core_abs));
    var_set("min", native_func_new("min", core_min));
    var_set("max", native_func_new("max", core_max));
    var_set("sum", native_func_new("sum", core_sum));
    var_set("sorted", native_func_new("sorted", core_sorted));
    var_set("reversed", native_func_new("reversed", core_reversed));
    var_set("float", native_func_new("float", core_float));
    var_set("chr", native_func_new("chr", core_chr));
    var_set("ord", native_func_new("ord", core_ord));
    var_set("bool", native_func_new("bool", core_bool));

    Value math = var_get("math");
    if (GET_TYPE(math) == VAL_DICT) {
        Dict *m = AS_DICT(math);
        dict_set(m, "exp", native_func_new("exp", m_exp));
        dict_set(m, "log", native_func_new("log", m_log));
        dict_set(m, "log10", native_func_new("log10", m_log10));
        dict_set(m, "tan", native_func_new("tan", m_tan));
        dict_set(m, "asin", native_func_new("asin", m_asin));
        dict_set(m, "acos", native_func_new("acos", m_acos));
        dict_set(m, "atan", native_func_new("atan", m_atan));
        dict_set(m, "atan2", native_func_new("atan2", m_atan2));
        dict_set(m, "hypot", native_func_new("hypot", m_hypot));
        dict_set(m, "abs", native_func_new("abs", core_abs));
        dict_set(m, "min", native_func_new("min", core_min));
        dict_set(m, "max", native_func_new("max", core_max));
        dict_set(m, "pi", value_float(3.14159265358979323846));
        dict_set(m, "e", value_float(2.71828182845904523536));
        dict_set(m, "inf", value_float(INFINITY));
    }
    /* os.env is set up with the scripting functions in process.c. */
    Value debug = var_get("debug");
    Value assert_fn = var_get("assert");
    if (GET_TYPE(debug) == VAL_DICT && GET_TYPE(assert_fn) != VAL_NONE) dict_set(AS_DICT(debug), "assert", assert_fn);
}
