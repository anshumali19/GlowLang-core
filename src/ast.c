#include "glowlang.h"
#include <stdlib.h>
#include <string.h>

extern const char *intern_string(const char *s);
extern void yyerror(const char *s);
extern Value value_string(const char *s);

/* The original compile function was here. I will replace it in glowlang.y directly for now 
   as it's tightly coupled with the parser state in that file. */
