%code requires {
    typedef struct Node {
        int type;
        int value;
        char* name;
        struct Node* left;
        struct Node* right;
        struct Node* third;
    } Node;
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser_ast3.tab.h"

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
#define LT 10
#define GT 11
#define IF 12
#define WHILE 13
#define CALL 14
#define METHOD_DEF 15

int vars[26];

/* Method table */
typedef struct {
    char name[50];
    Node* body;
} Method;

Method methods[20];
int methodCount = 0;

/* Node creators */
Node* newNode(int type, Node* left, Node* right) {
    Node* n = malloc(sizeof(Node));
    n->type = type;
    n->left = left;
    n->right = right;
    n->third = NULL;
    return n;
}

Node* newIf(Node* cond, Node* tbranch, Node* fbranch) {
    Node* n = malloc(sizeof(Node));
    n->type = IF;
    n->left = cond;
    n->right = tbranch;
    n->third = fbranch;
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

Node* newCall(char* name) {
    Node* n = malloc(sizeof(Node));
    n->type = CALL;
    n->name = strdup(name);
    return n;
}

/* Method registry */
void addMethod(char* name, Node* body) {
    strcpy(methods[methodCount].name, name);
    methods[methodCount].body = body;
    methodCount++;
}

Node* getMethod(char* name) {
    for(int i=0;i<methodCount;i++) {
        if(strcmp(methods[i].name,name)==0)
            return methods[i].body;
    }
    return NULL;
}

/* AST evaluator */
int eval(Node* n) {
    switch(n->type) {
        case NUM: return n->value;
        case VAR: return vars[n->name[0]-'a'];

        case ADD: return eval(n->left)+eval(n->right);
        case SUB: return eval(n->left)-eval(n->right);
        case MUL: return eval(n->left)*eval(n->right);
        case DIV: return eval(n->left)/eval(n->right);

        case LT: return eval(n->left)<eval(n->right);
        case GT: return eval(n->left)>eval(n->right);

        case ASSIGN:
            vars[n->left->name[0]-'a']=eval(n->right);
            return vars[n->left->name[0]-'a'];

        case PRINT:
            printf("%d\n", eval(n->left));
            return 0;

        case SEQ:
            eval(n->left);
            eval(n->right);
            return 0;

        case IF:
            if(eval(n->left)) eval(n->right);
            else if(n->third) eval(n->third);
            return 0;

        case WHILE:
            while(eval(n->left)) eval(n->right);
            return 0;

        case CALL: {
            Node* body = getMethod(n->name);
            if(body) eval(body);
            return 0;
        }
    }
    return 0;
}
%}

%union {
    int num;
    char* str;
    Node* node;
}

%token READY STOP LET PRINT IF ELSE WHILE METHOD CALL
%token PLUS MINUS MUL DIV ASSIGN
%token LT GT
%token <num> NUMBER
%token <str> ID

%type <node> expr stmt stmt_list program condition

%left PLUS MINUS
%left MUL DIV

%%

program:
    READY stmt_list STOP
    {
        printf("Executing AST Phase 3...\n");
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
    | IF condition stmt_list ELSE stmt_list STOP
      {
          $$ = newIf($2, $3, $5);
      }
    | WHILE condition stmt_list STOP
      {
          $$ = newNode(WHILE, $2, $3);
      }
    | METHOD ID stmt_list STOP
      {
          addMethod($2, $3);
          $$ = newNum(0);
      }
    | CALL ID
      {
          $$ = newCall($2);
      }
;

condition:
      expr LT expr { $$ = newNode(LT, $1, $3); }
    | expr GT expr { $$ = newNode(GT, $1, $3); }
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
    printf("Running GlowLang AST Phase 3...\n");
    return yyparse();
}
