%{
#include "parser_v3.tab.h"
#include <stdlib.h>
#include <string.h>
%}

%%
"ready"     return READY;
"stop"      return STOP;
"let"       return LET;
"print"     return PRINT;

"+"         return PLUS;
"-"         return MINUS;
"*"         return MUL;
"/"         return DIV;
"="         return ASSIGN;

\"[^\"]*\" {
    char* temp = strdup(yytext);
    temp[strlen(temp)-1] = '\0';
    yylval.str = strdup(temp + 1);
    free(temp);
    return STRING;
}

[0-9]+ {
    yylval.num = atoi(yytext);
    return NUMBER;
}

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    return ID;
}

[ \t\n]+ ;
. ;
%%
