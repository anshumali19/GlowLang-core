%{
#include "parser_ast.tab.h"
#include <stdlib.h>
#include <string.h>
%}

%%
"ready" return READY;
"stop" return STOP;
"let" return LET;
"print" return PRINT;

"+" return PLUS;
"-" return MINUS;
"*" return MUL;
"/" return DIV;
"=" return ASSIGN;

[0-9]+ {
    yylval.num = atoi(yytext);
    return NUMBER;
}

[a-z] {
    yylval.str = strdup(yytext);
    return ID;
}

[ \t\n]+ ;
. ;
%%
