%{
#include "parser_ast2.tab.h"
#include <stdlib.h>
#include <string.h>
%}

%%
"ready" return READY;
"stop" return STOP;
"let" return LET;
"print" return PRINT;
"if" return IF;
"else" return ELSE;
"while" return WHILE;

"+" return PLUS;
"-" return MINUS;
"*" return MUL;
"/" return DIV;
"=" return ASSIGN;
"<" return LT;
">" return GT;

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
