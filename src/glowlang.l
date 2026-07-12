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

#define INTERN_TABLE_SIZE 65536
static char *intern_table[INTERN_TABLE_SIZE];
static int intern_initialized = 0;

static void ensure_intern_init(void) {
    if (!intern_initialized) {
        memset(intern_table, 0, sizeof(intern_table));
        intern_initialized = 1;
    }
}

static char *arena_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = arena_alloc(len);
    memcpy(dup, s, len);
    return dup;
}

const char *intern_string(const char *s) {
    ensure_intern_init();
    unsigned int h = 2166136261u;
    for (int i = 0; s[i]; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619;
    }
    unsigned int start = h % INTERN_TABLE_SIZE;
    unsigned int idx = start;
    
    do {
        if (!intern_table[idx]) {
            char *dup = arena_strdup(s);
            intern_table[idx] = dup;
            return (const char *)dup;
        }
        if (strcmp(intern_table[idx], s) == 0)
            return intern_table[idx];
        idx = (idx + 1) % INTERN_TABLE_SIZE;
    } while (idx != start);

    return arena_strdup(s);
}

/* Intern a string AND return the FNV-1a hash it already computed.
   This is the same hash that hash_string() in stdlib.c produces,
   so dict callers can reuse it instead of hashing twice. */
const char *intern_string_hash(const char *s, uint32_t *out_hash) {
    ensure_intern_init();
    unsigned int h = 2166136261u;
    for (int i = 0; s[i]; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619;
    }
    *out_hash = (uint32_t)h;
    unsigned int start = h % INTERN_TABLE_SIZE;
    unsigned int idx = start;
    
    do {
        if (!intern_table[idx]) {
            char *dup = arena_strdup(s);
            intern_table[idx] = dup;
            return (const char *)dup;
        }
        if (strcmp(intern_table[idx], s) == 0)
            return intern_table[idx];
        idx = (idx + 1) % INTERN_TABLE_SIZE;
    } while (idx != start);

    return arena_strdup(s);
}

void intern_cleanup(void) {
    /* Handled by arena_free_all() now */
    memset(intern_table, 0, sizeof(intern_table));
    intern_initialized = 0;
}

void intern_table_init(void) {
    memset(intern_table, 0, sizeof(intern_table));
    intern_initialized = 1;
}

int line_num = 1;
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
"input"                { return INPUT_KW; }
"ask"                  { return INPUT_KW; }
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
"["                    { return '['; }
"]"                    { return ']'; }
"{"                    { return '{'; }
"}"                    { return '}'; }
","                    { return COMMA; }
"("                    { return '('; }
")"                    { return ')'; }

[0-9]+\.[0-9]+ {
    yylval.float_val = atof(yytext);
    return FLOAT_LIT;
}

[0-9]+ {
    yylval.str_val = intern_string(yytext);
    return INT_LIT;
}

\"\"\"[^\"]*\"\"\" {
    /* Simple triple-quoted string */
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

\"([^"\\]|\\.)*\" {
    char *raw = strdup(yytext + 1);
    raw[strlen(raw) - 1] = '\0';
    char *out = malloc(strlen(raw) + 1);
    char *p = out, *r = raw;
    while (*r) {
        if (*r == '\\' && *(r + 1)) {
            r++;
            switch (*r) {
                case '"':  *p++ = '"';  break;
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

\n                     { line_num++; return NEWLINE; }
[ \t\r]+               ;

"//"[^\n]*             ;
"#"[^\n]*              ;

"/*"                   { BEGIN(COMMENT); }
<COMMENT>"*/"          { BEGIN(INITIAL); }
<COMMENT>\n            { line_num++; }
<COMMENT>.             ;

.                      { printf("Warning: unknown char '%c' (0x%02x) at line %d\n", *yytext, *yytext, line_num); }

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
