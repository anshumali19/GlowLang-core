%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int yylex();
void yyerror(const char *s);

typedef struct {
    char name[50];
    int value;
} Variable;

Variable table[100];
int varCount = 0;

int getVar(char *name) {
    for (int i = 0; i < varCount; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].value;
    }
    return 0;
}

void setVar(char *name, int value) {
    for (int i = 0; i < varCount; i++) {
        if (strcmp(table[i].name, name) == 0) {
            table[i].value = value;
            return;
        }
    }
    strcpy(table[varCount].name, name);
    table[varCount].value = value;
    varCount++;
}
%}

%union {
    int num;
    char* str;
}

%token READY STOP LET PRINT IF EIF ELSE WHILE
%token PLUS MINUS MUL DIV
%token ASSIGN
%token LT GT LE GE EQ
%token <num> NUMBER
%token <str> ID

%left PLUS MINUS
%left MUL DIV

%type <num> expr

%%

program:
    READY stmt_list STOP
    {
        printf("GlowLang Phase 2 executed successfully\n");
    }
;

stmt_list:
      stmt_list stmt
    | stmt
;

stmt:
      LET ID ASSIGN expr
      {
          setVar($2, $4);
      }
    | PRINT expr
      {
          printf("%d\n", $2);
      }
;

expr:
      expr PLUS expr   { $$ = $1 + $3; }
    | expr MINUS expr  { $$ = $1 - $3; }
    | expr MUL expr    { $$ = $1 * $3; }
    | expr DIV expr    { $$ = $1 / $3; }
    | NUMBER           { $$ = $1; }
    | ID               { $$ = getVar($1); }
;

%%

void yyerror(const char *s) {
    printf("GlowLang Error: %s\n", s);
}

int main() {
    printf("Running GlowLang Phase 2...\n");
    return yyparse();
}
