#include "glowlang.h"
#include <stdlib.h>
#include <string.h>

extern const char *intern_string(const char *s);
extern void yyerror(const char *s);
extern Value value_string(const char *s);

