%code requires {
    typedef struct GlowValue {
        int type;
        int num;
        char* str;
    } Value;
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser_v3.tab.h"

int yylex();
void yyerror(const char *s);

#define INT 1
#define STR 2

typedef struct {
    char name[50];
    Value value;
} Variable;

Variable table[100];
int varCount = 0;

Value getVar(char *name) {
    for (int i = 0; i < varCount; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].value;
    }

    Value v;
    v.type = INT;
    v.num = 0;
    v.str = NULL;
    return v;
}

void setVar(char *name, Value val) {
    for (int i = 0; i < varCount; i++) {
        if (strcmp(table[i].name, name) == 0) {
            table[i].value = val;
            return;
        }
    }

    strcpy(table[varCount].name, name);
    table[varCount].value = val;
    varCount++;
}
%}

%union {
    int num;
    char* str;
    Value val;
}

%token READY STOP LET PRINT
%token PLUS MINUS MUL DIV ASSIGN
%token <num> NUMBER
%token <str> ID STRING

%type <val> expr

%left PLUS MINUS
%left MUL DIV

%%

program:
    READY stmt_list STOP
    {
        printf("GlowLang Phase 3 executed successfully\n");
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
          if ($2.type == INT)
              printf("%d\n", $2.num);
          else if ($2.type == STR)
              printf("%s\n", $2.str);
      }
;

expr:
      expr PLUS expr
      {
          if ($1.type == INT && $3.type == INT) {
              $$.type = INT;
              $$.num = $1.num + $3.num;
          }
          else if ($1.type == STR && $3.type == STR) {
              $$.type = STR;
              char* res = malloc(strlen($1.str) + strlen($3.str) + 1);
              strcpy(res, $1.str);
              strcat(res, $3.str);
              $$.str = res;
          }
      }
    | expr MINUS expr
      {
          $$.type = INT;
          $$.num = $1.num - $3.num;
      }
    | expr MUL expr
      {
          $$.type = INT;
          $$.num = $1.num * $3.num;
      }
    | expr DIV expr
      {
          $$.type = INT;
          $$.num = $1.num / $3.num;
      }
    | NUMBER
      {
          $$.type = INT;
          $$.num = $1;
      }
    | STRING
      {
          $$.type = STR;
          $$.str = $1;
      }
    | ID
      {
          $$ = getVar($1);
      }
;

%%

void yyerror(const char *s) {
    printf("GlowLang Error: %s\n", s);
}

int main() {
    printf("Running GlowLang Phase 3...\n");
    return yyparse();
}
