%{
#include "glowlang.h"
#include "glowlang.tab.h"
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

/* String interning: identifiers and dict keys map to one canonical char* so
   dicts can compare keys by pointer. Open addressing with the hash stored per
   slot; the table doubles at 50% load, so lookups stay O(1) however many
   distinct keys a program creates. Protected by the GIL. */
typedef struct { const char *str; uint32_t hash; } InternSlot;
static InternSlot *intern_slots = NULL;
static uint32_t intern_cap = 0;
static uint32_t intern_count = 0;

static char *arena_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = arena_alloc(len);
    memcpy(dup, s, len);
    return dup;
}

/* FNV-1a; the same hash that hash_string() in stdlib.c produces, so dict
   callers can reuse it instead of hashing twice. */
static uint32_t intern_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (int i = 0; s[i]; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static void intern_grow(void) {
    uint32_t new_cap = intern_cap ? intern_cap * 2 : 4096;
    InternSlot *ns = calloc(new_cap, sizeof(InternSlot));
    uint32_t mask = new_cap - 1;
    for (uint32_t i = 0; i < intern_cap; i++) {
        if (!intern_slots[i].str) continue;
        uint32_t j = intern_slots[i].hash & mask;
        while (ns[j].str) j = (j + 1) & mask;
        ns[j] = intern_slots[i];
    }
    free(intern_slots);
    intern_slots = ns;
    intern_cap = new_cap;
}

static const char *intern_lookup(const char *s, uint32_t h, int insert) {
    if (!intern_slots) intern_grow();
    if (insert && (intern_count + 1) * 2 > intern_cap) intern_grow();
    uint32_t mask = intern_cap - 1;
    uint32_t i = h & mask;
    while (intern_slots[i].str) {
        if (intern_slots[i].hash == h && strcmp(intern_slots[i].str, s) == 0) return intern_slots[i].str;
        i = (i + 1) & mask;
    }
    if (!insert) return NULL;
    char *dup = arena_strdup(s);
    intern_slots[i].str = dup;
    intern_slots[i].hash = h;
    intern_count++;
    return dup;
}

const char *intern_string(const char *s) {
    return intern_lookup(s, intern_hash(s), 1);
}

/* Intern a string AND return its hash. */
const char *intern_string_hash(const char *s, uint32_t *out_hash) {
    uint32_t h = intern_hash(s);
    *out_hash = h;
    return intern_lookup(s, h, 1);
}

/* Look a string up without interning it: NULL means no dict can contain it
   as a key, so lookups of missing keys never grow the table. */
const char *intern_string_find(const char *s, uint32_t *out_hash) {
    uint32_t h = intern_hash(s);
    *out_hash = h;
    return intern_lookup(s, h, 0);
}

void intern_cleanup(void) {
    /* The strings themselves live in the arena (freed by arena_free_all). */
    free(intern_slots);
    intern_slots = NULL;
    intern_cap = 0;
    intern_count = 0;
}

void intern_table_init(void) {
    if (!intern_slots) intern_grow();
}

int line_num = 1;
/* Open (, [ and { not yet closed: newlines inside them do not end the
   statement, so literals and calls can span lines. Reset by the parser. */
int lexer_bracket_depth = 0;
%}

%option noinput nounput nounistd noyywrap
%x COMMENT

%%

"show"                 { return SHOW; }
"print"                { return SHOW; }
"write"                { return WRITE; }
"when"                 { return WHEN; }
"if"                   { return WHEN; }
"eif"                  { return EIF; }
"otherwise"            { return OTHERWISE; }
"else"                 { return OTHERWISE; }
"while"                { return WHILE; }
"until"                { return UNTIL; }
"repeat"               { return REPEAT; }
"times"                { return TIMES_KW; }
"for"                  { return FOR; }
"every"                { return FOR; }
"each"                 { return EACH; }
"in"                   { return IN_KW; }
"is"                   { return IS; }
"is not"               { return IS_NOT; }
"from"                 { return FROM; }
"to"                   { return TO; }
"func"                 { return FUNC; }
"action"               { return FUNC; }
"can"                  { return FUNC; }
"lambda"               { return LAMBDA; }
"let"                  { return LET; }
"return"               { return RETURN; }
"true"                 { return TRUE_KW; }
"false"                { return FALSE_KW; }
"and"                  { return AND_OP; }
"or"                   { return OR_OP; }
"not"                  { return NOT_OP; }
"stop"                 { return STOP; }
"break"                { return STOP; }
"skip"                 { return SKIP; }
"continue"             { return SKIP; }
"try"                  { return TRY; }
"catch"                { return CATCH; }
"throw"                { return THROW; }
"import"               { return IMPORT; }
"class"                { return CLASS; }
"blueprint"            { return CLASS; }
"Blueprint"            { return CLASS; }
"trait"                { return TRAIT; }
"extends"              { return EXTENDS; }
"use"                  { return USE; }
"spawn"                { return SPAWN; }
"whenever"             { return WHENEVER; }
"end"                  { return END; }

"+="                   { return PLUS_ASSIGN; }
"-="                   { return MINUS_ASSIGN; }
"*="                   { return MUL_ASSIGN; }
"/="                   { return DIV_ASSIGN; }
"%="                   { return MOD_ASSIGN; }

"=="                   { return EQ; }
"!="                   { return NEQ; }
"<="                   { return LTE; }
">="                   { return GTE; }
"**"                   { return POW; }

":"                    { return COLON; }
"."                    { return '.'; }
"<"                    { return LT; }
">"                    { return GT; }
"+"                    { return PLUS; }
"-"                    { return MINUS; }
"*"                    { return MUL; }
"/"                    { return DIV; }
"%"                    { return MOD; }
"="                    { return ASSIGN; }
"["                    { lexer_bracket_depth++; return '['; }
"]"                    { if (lexer_bracket_depth > 0) lexer_bracket_depth--; return ']'; }
"{"                    { lexer_bracket_depth++; return '{'; }
"}"                    { if (lexer_bracket_depth > 0) lexer_bracket_depth--; return '}'; }
","                    { return COMMA; }
"("                    { lexer_bracket_depth++; return '('; }
")"                    { if (lexer_bracket_depth > 0) lexer_bracket_depth--; return ')'; }

[0-9]+(\.[0-9]+)?[eE][+-]?[0-9]+ {
    /* Scientific notation: 1e15, 2.5e-3 */
    yylval.float_val = atof(yytext);
    return FLOAT_LIT;
}

[0-9]+\.[0-9]+ {
    yylval.float_val = atof(yytext);
    return FLOAT_LIT;
}

[0-9]+ {
    yylval.str_val = intern_string(yytext);
    return INT_LIT;
}

\"\"\"[^\"]*\"\"\" {
    /* Simple triple-quoted string. Count its newlines, so errors further
       down the file report the right line. */
    for (int i = 0; i < yyleng; i++) if (yytext[i] == '\n') line_num++;
    int len = yyleng - 6;
    char *out = malloc(len + 1);
    strncpy(out, yytext + 3, len);
    out[len] = '\0';
    yylval.str_val = intern_string(out);
    free(out);
    return STRING;
}

f\"([^"\\]|\\.)*\" {
    /* Simple f-string for now */
    char *out = strdup(yytext + 2);
    out[strlen(out)-1] = '\0';
    yylval.str_val = intern_string(out);
    free(out);
    return FSTRING;
}

\"([^"\\]|\\.)*\" |
'([^'\\\n]|\\.)*' {
    /* Double- or single-quoted; single quotes make JSON easy to write. */
    char *raw = strdup(yytext + 1);
    raw[strlen(raw) - 1] = '\0';
    char *out = malloc(strlen(raw) + 1);
    char *p = out, *r = raw;
    while (*r) {
        if (*r == '\\' && *(r + 1)) {
            r++;
            switch (*r) {
                case '"':  *p++ = '"';  break;
                case '\'': *p++ = '\''; break;
                case '\\': *p++ = '\\'; break;
                case 'n':  *p++ = '\n'; break;
                case 't':  *p++ = '\t'; break;
                case 'r':  *p++ = '\r'; break;
                default:   *p++ = '\\'; *p++ = *r; break;
            }
            r++;
        } else {
            *p++ = *r++;
        }
    }
    *p = '\0';
    yylval.str_val = intern_string(out);
    free(raw);
    free(out);
    return STRING;
}

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str_val = (const char *)intern_string(yytext);
    return ID;
}

\n                     { line_num++; if (lexer_bracket_depth == 0) return NEWLINE; }
[ \t\r]+               ;
 /* The UTF-8 byte order mark that Notepad and PowerShell 5 put at the start
    of files: skip it like whitespace (outside strings) wherever it appears,
    so files from every entry point (run, import, eval) read the same. */
"\xEF\xBB\xBF"         ;

"//"[^\n]*             ;
"#"[^\n]*              ;

"/*"                   { BEGIN(COMMENT); }
<COMMENT>"*/"          { BEGIN(INITIAL); }
<COMMENT>\n            { line_num++; }
<COMMENT>.             ;

.                      { printf("Warning: unknown char '%c' (0x%02x) at line %d\n", *yytext, (unsigned char)*yytext, line_num); }

%%



void lexer_cleanup(void) {
    #ifdef YY_CURRENT_BUFFER
    if (YY_CURRENT_BUFFER) {
        yy_delete_buffer(YY_CURRENT_BUFFER);
    }
    #endif
    yylex_destroy();
}

YY_BUFFER_STATE get_current_buffer(void) {
    #ifdef YY_CURRENT_BUFFER
    return YY_CURRENT_BUFFER;
    #else
    return NULL;
    #endif
}
