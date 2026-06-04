#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/glowlang.h"

// External Lexer/Parser functions
typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern int yyparse(void);
extern void glow_clear_errors(void);

// Dummy node root for the parser
extern Node *root;

void fuzz_iteration(const char *input) {
    root = NULL;
    glow_clear_errors();
    
    YY_BUFFER_STATE bp = yy_scan_string(input);
    if (bp) {
        yyparse();
        yy_delete_buffer(bp);
    }
    
    // In a real fuzzer, we'd recursively free 'root' here.
    // For now, we're testing for parser crashes during the run.
}

int main() {
    srand(time(NULL));
    const char *chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 \n\t(){}[],.+-*/%=!<>|&;\"' let func can blueprint if while return show ";
    int char_count = strlen(chars);

    printf("--- Starting Parser Fuzzing (1000 iterations) ---\n");

    for (int i = 0; i < 1000; i++) {
        int len = rand() % 512;
        char *buf = malloc(len + 1);
        for (int j = 0; j < len; j++) {
            buf[j] = chars[rand() % char_count];
        }
        buf[len] = '\0';

        if (i % 100 == 0) printf("Iteration %d...\n", i);
        
        fuzz_iteration(buf);
        free(buf);
    }

    printf("--- Fuzzing Completed Successfully ---\n");
    return 0;
}
