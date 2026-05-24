%{
#include "parser_v4.tab.h"
#include <stdlib.h>
#include <string.h>
%}

%%
"ready"     return READY;
"stop"      return STOP;
"let"       return LET;
"print"     return PRINT;
"if"        return IF;
"else"      return ELSE;

"=="        return EQ;
">"         return GT;
"<"         return LT;

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
