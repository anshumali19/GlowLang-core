%{
#include "parser_ast4.tab.h"
#include <stdlib.h>
#include <string.h>
%}

%%
"ready" return READY;
"stop" return STOP;
"let" return LET;
"print" return PRINT;
"method" return METHOD;
"call" return CALL;
"return" return RETURN;

"+" return PLUS;
"-" return MINUS;
"*" return MUL;
"/" return DIV;
"=" return ASSIGN;

[0-9]+ {
    yylval.num = atoi(yytext);
    return NUMBER;
}

[a-z]+ {
    yylval.str = strdup(yytext);
    return ID;
}

[ \t\n]+ ;
. ;
%%
