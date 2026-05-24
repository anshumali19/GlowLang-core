%{
#include "parser.tab.h"
#include <stdlib.h>
%}

%%
"ready"     return READY;
"stop"      return STOP;
"let"       return LET;
"print"     return PRINT;
"if"        return IF;
"eif"       return EIF;
"else"      return ELSE;
"while"     return WHILE;

"=="        return EQ;
"<="        return LE;
">="        return GE;
"<"         return LT;
">"         return GT;
"="         return ASSIGN;

"+"         return PLUS;
"-"         return MINUS;
"*"         return MUL;
"/"         return DIV;

[0-9]+      { yylval.num = atoi(yytext); return NUMBER; }
[a-z]       { yylval.id = yytext[0]; return ID; }

[ \t\n]     ;
.           ;

%%
