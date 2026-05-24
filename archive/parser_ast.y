%code requires {
    typedef struct Node {
        int type;
        int value;
        char* name;
        struct Node* left;
        struct Node* right;
    } Node;
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser_ast.tab.h"

int yylex();
void yyerror(const char *s);

/* Node types */
#define NUM 1
#define VAR 2
#define ADD 3
#define SUB 4
#define MUL 5
#define DIV 6
#define ASSIGN 7
#define PRINT 8
#define SEQ 9

/* Symbol table */
int vars[100];

/* Create nodes */
Node* newNode(int type, Node* left, Node* right) {
    Node* n = malloc(sizeof(Node));
    n->type = type;
    n->left = left;
    n->right = right;
    return n;
}

Node* newNum(int value) {
    Node* n = malloc(sizeof(Node));
    n->type = NUM;
    n->value = value;
    return n;
}

Node* newVar(char* name) {
    Node* n = malloc(sizeof(Node));
    n->type = VAR;
    n->name = strdup(name);
    return n;
}

/* Interpreter */
int eval(Node* n) {
    switch(n->type) {
        case NUM: return n->value;

        case VAR:
            return vars[n->name[0] - 'a'];

        case ADD:
            return eval(n->left) + eval(n->right);

        case SUB:
            return eval(n->left) - eval(n->right);

        case MUL:
            return eval(n->left) * eval(n->right);

        case DIV:
            return eval(n->left) / eval(n->right);

        case ASSIGN: {
            int val = eval(n->right);
            vars[n->left->name[0] - 'a'] = val;
            return val;
        case SEQ:
    	    eval(n->left);
            eval(n->right);
            return 0;
        }

        case PRINT:
            printf("%d\n", eval(n->left));
            return 0;
    }
    return 0;
}
%}

%union {
    int num;
    char* str;
    Node* node;
}

%token READY STOP LET PRINT
%token PLUS MINUS MUL DIV ASSIGN
%token <num> NUMBER
%token <str> ID

%type <node> expr stmt stmt_list program

%left PLUS MINUS
%left MUL DIV

%%

program:
    READY stmt_list STOP
    {
        printf("Executing AST...\n");
        eval($2);
    }
;

stmt_list:
      stmt_list stmt
      {
          $$ = newNode(SEQ, $1, $2);
      }
    | stmt
      {
          $$ = $1;
      }
;

stmt:
      LET ID ASSIGN expr
      {
          $$ = newNode(ASSIGN, newVar($2), $4);
      }
    | PRINT expr
      {
          $$ = newNode(PRINT, $2, NULL);
      }
;

expr:
      expr PLUS expr { $$ = newNode(ADD, $1, $3); }
    | expr MINUS expr { $$ = newNode(SUB, $1, $3); }
    | expr MUL expr { $$ = newNode(MUL, $1, $3); }
    | expr DIV expr { $$ = newNode(DIV, $1, $3); }
    | NUMBER { $$ = newNum($1); }
    | ID { $$ = newVar($1); }
;

%%

void yyerror(const char *s) {
    printf("AST Error: %s\n", s);
}

int main() {
    printf("Running GlowLang AST Phase...\n");
    return yyparse();
}
