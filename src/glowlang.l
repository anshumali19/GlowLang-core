%{
#include "glowlang.h"
#include "glowlang.tab.h"
#include <stdlib.h>
#include <string.h>

#define INTERN_TABLE_SIZE 4096
static char *intern_table[INTERN_TABLE_SIZE];
static int intern_initialized = 0;

static void ensure_intern_init(void) {
    if (!intern_initialized) {
        memset(intern_table, 0, sizeof(intern_table));
        intern_initialized = 1;
    }
}

const char *intern_string(const char *s) {
    ensure_intern_init();
    if (!s || !*s) return "";
    unsigned int h = 5381;
    const char *p = s;
    while (*p) h = ((h << 5) + h) + (unsigned char)*p++;
    unsigned int idx = h % INTERN_TABLE_SIZE;

    unsigned int start = idx;
    do {
        if (!intern_table[idx]) {
            char *dup = strdup(s);
            intern_table[idx] = dup;
            return (const char *)dup;
        }
        if (strcmp(intern_table[idx], s) == 0)
            return intern_table[idx];
        idx = (idx + 1) % INTERN_TABLE_SIZE;
    } while (idx != start);

    return strdup(s);
}

void intern_cleanup(void) {
    for (int i = 0; i < INTERN_TABLE_SIZE; i++) {
        if (intern_table[i]) { free(intern_table[i]); intern_table[i] = NULL; }
    }
    intern_initialized = 0;
}

void intern_table_init(void) {
    memset(intern_table, 0, sizeof(intern_table));
    intern_initialized = 1;
}

int line_num = 1;
%}

%x COMMENT

%%

"show"                 { return SHOW; }
"print"                { return SHOW; }
"write"                { return WRITE; }
"when"                 { return WHEN; }
"if"                   { return WHEN; }
"eif"                  { return EIF; }
"otherwise"            { return OTHERWISE; }
"while"                { return WHILE; }
"until"                { return UNTIL; }
"repeat"               { return REPEAT; }
"times"                { return TIMES_KW; }
"input"                { return INPUT_KW; }
"ask"                  { return INPUT_KW; }
"for"                  { return FOR; }
"every"                { return FOR; }
"each"                 { return EACH; }
"in"                   { return IN; }
"is"                   { return IS; }
"is not"               { return IS_NOT; }
"from"                 { return FROM; }
"to"                   { return TO; }
"func"                 { return FUNC; }
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
"model"                { return CLASS; }
"blueprint"            { return CLASS; }
"trait"                { return TRAIT; }
"ability"              { return TRAIT; }
"behavior"             { return TRAIT; }
"extends"              { return EXTENDS; }
"like"                 { return EXTENDS; }
"use"                  { return USE; }
"can"                  { return USE; }
"spawn"                { return SPAWN; }
"start"                { return SPAWN; }
"action"               { return FUNC; }
"task"                 { return FUNC; }
"routine"              { return FUNC; }


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
    yylval.int_val = atoi(yytext);
    return INT_LIT;
}

\"\"\"([^\"]|\"([^\"]|\"[^\"]))*\"\"\" {
    /* Triple-quoted string: """...""" */
    int len = yyleng - 6; /* remove """ at start and end */
    char *out = malloc(len + 1);
    strncpy(out, yytext + 3, len);
    out[len] = '\0';
    yylval.str_val = intern_string(out);
    free(out);
    return STRING;
}

f\"([^\"]|\\.)*\" {
    /* F-string: f"hello {name}" - pass raw content to parser */
    /* Skip the leading f" and trailing " */
    char *raw = strdup(yytext + 2);
    raw[strlen(raw) - 1] = '\0';
    yylval.str_val = intern_string(raw);
    free(raw);
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

#[^\n]*                ;

"/*"                   { BEGIN(COMMENT); }
<COMMENT>"*/"          { BEGIN(INITIAL); }
<COMMENT>\n            { line_num++; }
<COMMENT>.             ;

.                      { printf("Warning: unknown char '%c' (0x%02x) at line %d\n", *yytext, *yytext, line_num); }

%%

int yywrap(void) {
    return 1;
}
