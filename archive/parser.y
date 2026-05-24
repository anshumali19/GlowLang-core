%{
#include <stdio.h>
#include <stdlib.h>

int yylex();
void yyerror(const char *s);

int vars[26];
%}

%union {
    int num;
    char id;
}

%token READY STOP LET PRINT IF EIF ELSE WHILE
%token PLUS MINUS MUL DIV
%token ASSIGN
%token LT GT LE GE EQ
%token <num> NUMBER
%token <id> ID

%left PLUS MINUS
%left MUL DIV
%left LT GT LE GE EQ

%type <num> expr

%%

program:
    READY stmt_list STOP
    {
        printf("GlowLang program executed successfully\n");
    }
;

stmt_list:
      stmt_list stmt
    | stmt
;

stmt:
      LET ID ASSIGN expr
      {
          vars[$2 - 'a'] = $4;
      }
    | PRINT expr
      {
          printf("%d\n", $2);
      }
    | IF condition stmt_list STOP
    | WHILE condition stmt_list STOP
;

condition:
      expr LT expr
    | expr GT expr
    | expr LE expr
    | expr GE expr
    | expr EQ expr
;

expr:
      expr PLUS expr   { $$ = $1 + $3; }
    | expr MINUS expr  { $$ = $1 - $3; }
    | expr MUL expr    { $$ = $1 * $3; }
    | expr DIV expr    { $$ = $1 / $3; }
    | NUMBER           { $$ = $1; }
    | ID               { $$ = vars[$1 - 'a']; }
;

%%

void yyerror(const char *s) {
    printf("GlowLang Error: %s\n", s);
}

int main() {
    printf("Running GlowLang...\n");
    return yyparse();
}
