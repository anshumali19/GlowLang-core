%code requires {
    #include "glowlang.h"
}

%{#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#else
#ifndef _WIN32
#include <unistd.h>
#endif
#endif
#include "glowlang.h"
#include "lsp.h"

extern int yylex(void);
extern void lexer_cleanup(void);

#ifdef _WIN32
#include <windows.h>
static int get_self_exe_path(char *path, size_t max_len) {
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)max_len);
    return (len > 0) ? (int)len : -1;
}
#else
static int get_self_exe_path(char *path, size_t max_len) {
    return (int)readlink("/proc/self/exe", path, max_len);
}
#endif
%}

%union {
    int64_t int_val;
    double float_val;
    const char *str_val;
    struct Node *node;
    struct { struct Node **nodes; int count; } node_list;
    struct { struct Node **keys; struct Node **values; int count; } dict_list;
    struct { char **names; int count; } str_list;
}

%token <str_val> INT_LIT
%token <float_val> FLOAT_LIT
%token <str_val> ID STRING FSTRING
%token SHOW WRITE WHEN WHENEVER EIF OTHERWISE WHILE UNTIL REPEAT FOR EACH IN_KW FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN TRUE_KW FALSE_KW TIMES_KW INPUT_KW IS IS_NOT FROM TO NEWLINE COMMA ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN EQ NEQ LT GT LTE GTE PLUS MINUS MUL DIV MOD POW COLON AND_OP OR_OP NOT_OP

%type <node> program lines stmt expr term power postfix primary condition eif_chain otherwise_part unary
%type <node_list> expr_list arg_list
%type <dict_list> dict_elements
%type <str_list> param_list
%type <str_val> member_name

%left OR_OP
%left AND_OP
%left EQ NEQ IS IS_NOT
%left LT GT LTE GTE
%left PLUS MINUS
%left MUL DIV MOD
%right POW
%nonassoc NOT_OP

%%

program: lines { root = $1; };
lines: lines NEWLINE stmt { $$ = newSeqNode($1, $3); } | lines NEWLINE { $$ = $1; } | stmt { $$ = $1; } | { $$ = NULL; };
stmt: postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else { $$ = $1; $$->type = NODE_INDEX_ASSIGN; $$->third = $3; } }
    | IMPORT STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; }
    | USE STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; }
    | RETURN expr { $$ = newReturnNode($2); }
    | STOP { $$ = node_new(NODE_STOP); }
    | SKIP { $$ = node_new(NODE_SKIP); }
    | FOR EACH ID IN_KW expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $3; $$->left = $5; $$->right = $8; }
    | FOR ID IN_KW expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $2; $$->left = $4; $$->right = $7; }
    | FOR ID FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $2; $$->left = $4; $$->right = $6; $$->third = $9; }
    | FOR ID IN_KW FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $2; $$->left = $5; $$->right = $7; $$->third = $10; }
    | FOR EACH ID IN_KW FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $3; $$->left = $6; $$->right = $8; $$->third = $11; }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | FUNC ID '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->parent_name = $4; $$->left = $7; }
    | TRAIT ID COLON NEWLINE lines END { $$ = node_new(NODE_TRAIT); $$->name = $2; $$->left = $5; }
    | USE ID { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }
    | WHENEVER condition COLON NEWLINE lines END { $$ = node_new(NODE_WHENEVER); $$->left = $2; $$->right = $5; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | UNTIL condition COLON NEWLINE lines END { $$ = newUntilNode($2, $5); }
    | REPEAT expr TIMES_KW COLON NEWLINE lines END { $$ = newRepeatNode($2, $6); }
    | postfix PLUS_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_ADD, $1->name, $3); else { yyerror("Compound assignment target must be a variable"); YYERROR; } }
    | postfix MINUS_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_SUB, $1->name, $3); else { yyerror("Compound assignment target must be a variable"); YYERROR; } }
    | postfix MUL_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_MUL, $1->name, $3); else { yyerror("Compound assignment target must be a variable"); YYERROR; } }
    | postfix DIV_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_DIV, $1->name, $3); else { yyerror("Compound assignment target must be a variable"); YYERROR; } }
    | postfix MOD_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_MOD, $1->name, $3); else { yyerror("Compound assignment target must be a variable"); YYERROR; } }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = $6; $$->right = $9; }
    | THROW expr { $$ = node_new(NODE_THROW); $$->left = $2; }
    | SPAWN expr { $$ = node_new(NODE_SPAWN); $$->left = $2; }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | WRITE expr_list { $$ = newWriteNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); };

expr_list: expr { $$.count = 1; $$.nodes = arena_alloc(sizeof(Node*)); $$.nodes[0] = $1; } | expr_list COMMA expr { $$.nodes = arena_realloc($$.nodes, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.nodes[$1.count] = $3; $$.count = $1.count + 1; };
arg_list: expr_list { $$ = $1; };
param_list: ID { $$.count = 1; $$.names = arena_alloc(sizeof(char*)); $$.names[0] = (char*)intern_string($1); } | param_list COMMA ID { $$.names = arena_realloc($$.names, sizeof(char*)*($1.count), sizeof(char*)*($1.count+1)); $$.names[$1.count] = (char*)intern_string($3); $$.count = $1.count + 1; };
condition: expr;
eif_chain: eif_chain EIF condition COLON NEWLINE lines { $$ = newSeqNode($1, newIfNode($3, $6, NULL)); } | { $$ = NULL; };
otherwise_part: OTHERWISE COLON NEWLINE lines { $$ = $4; } | { $$ = NULL; };

expr: term | expr PLUS expr { $$ = newBinOp(NODE_ADD, $1, $3); } | expr MINUS expr { $$ = newBinOp(NODE_SUB, $1, $3); } | expr EQ expr { $$ = newBinOp(NODE_EQ, $1, $3); } | expr NEQ expr { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr IS expr { $$ = newBinOp(NODE_EQ, $1, $3); } | expr IS_NOT expr { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr IS IN_KW expr { $$ = newBinOp(NODE_IN, $1, $4); } | expr IS_NOT IN_KW expr { $$ = newBinOp(NODE_NOT, newBinOp(NODE_IN, $1, $4), NULL); } | expr LT expr { $$ = newBinOp(NODE_LT, $1, $3); } | expr GT expr { $$ = newBinOp(NODE_GT, $1, $3); } | expr LTE expr { $$ = newBinOp(NODE_LTE, $1, $3); } | expr GTE expr { $$ = newBinOp(NODE_GTE, $1, $3); } | expr AND_OP expr { $$ = newBinOp(NODE_AND, $1, $3); } | expr OR_OP expr { $$ = newBinOp(NODE_OR, $1, $3); } | NOT_OP expr { $$ = newBinOp(NODE_NOT, $2, NULL); };
term: unary | term MUL unary { $$ = newBinOp(NODE_MUL, $1, $3); } | term DIV unary { $$ = newBinOp(NODE_DIV, $1, $3); } | term MOD unary { $$ = newBinOp(NODE_MOD, $1, $3); };
unary: power | MINUS unary { $$ = newBinOp(NODE_NEG, $2, NULL); };
power: postfix | postfix POW power { $$ = newBinOp(NODE_POW, $1, $3); };
postfix: primary | postfix '.' member_name { $$ = node_new(NODE_INDEX); $$->left = $1; $$->name = $3; } | postfix '(' arg_list ')' { $$ = newCallExprNode($1, $3.nodes, $3.count); } | postfix '(' ')' { $$ = newCallExprNode($1, NULL, 0); } | postfix '[' expr ']' { $$ = node_new(NODE_INDEX); $$->left = $1; $$->right = $3; }
       | postfix '[' expr COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = $1; $$->right = $3; $$->third = $5; }
       | postfix '[' expr COLON ']' { $$ = node_new(NODE_SLICE); $$->left = $1; $$->right = $3; $$->third = NULL; }
       | postfix '[' COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = $1; $$->right = NULL; $$->third = $4; }
       | postfix '[' COLON ']' { $$ = node_new(NODE_SLICE); $$->left = $1; $$->right = NULL; $$->third = NULL; };
member_name: ID { $$ = $1; }
           | WRITE { $$ = intern_string("write"); }
           | SHOW { $$ = intern_string("show"); }
           ;
primary: ID { $$ = newVarNode($1); } | INT_LIT { $$ = newIntNodeStr($1); free((void*)$1); } | STRING { $$ = newStringNode($1); } | FSTRING { $$ = node_new(NODE_FSTRING); $$->name = $1; } | FLOAT_LIT { $$ = newFloatNode($1); } | TRUE_KW { $$ = newBoolNode(1); } | FALSE_KW { $$ = newBoolNode(0); } | '(' expr ')' { $$ = $2; } | '[' expr FOR EACH ID IN_KW expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $5;
    $$->left = $7;
    $$->comp_filter = NULL;
} | '[' expr FOR EACH ID IN_KW expr WHEN expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $5;
    $$->left = $7;
    $$->comp_filter = $9;
} | '[' expr FOR ID IN_KW expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $4;
    $$->left = $6;
    $$->comp_filter = NULL;
} | '[' expr FOR ID IN_KW expr WHEN expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $4;
    $$->left = $6;
    $$->comp_filter = $8;
} | '[' expr_list ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = $2.nodes; $$->element_count = $2.count; } | '[' ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = NULL; $$->element_count = 0; } | '{' dict_elements '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = $2.keys; $$->dict_values = $2.values; $$->dict_count = $2.count; } | '{' '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = NULL; $$->dict_values = NULL; $$->dict_count = 0; } | LAMBDA '(' param_list ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, newReturnNode($6)); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, newReturnNode($5)); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' param_list ')' COLON NEWLINE lines END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, $7); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' ')' COLON NEWLINE lines END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, $6); n->type = NODE_LAMBDA; $$ = n; };

dict_elements: expr COLON expr { $$.count = 1; $$.keys = arena_alloc(sizeof(Node*)); $$.keys[0] = $1; $$.values = arena_alloc(sizeof(Node*)); $$.values[0] = $3; } | dict_elements COMMA expr COLON expr { $$.keys = arena_realloc($$.keys, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.keys[$1.count] = $3; $$.values = arena_realloc($$.values, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.values[$1.count] = $5; $$.count = $1.count + 1; };


%%
Node *root = NULL;
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
void dump_all_bc() {
    printf("Main:\n");
    if (main_fe.body) {
        print_bytecode(main_fe.body);
    }
    for (int i = 0; i < func_table_size; i++) {
        if (func_table && func_table[i].used && func_table[i].body) {
            printf("\nFunction '%s':\n", func_table[i].name);
            print_bytecode(func_table[i].body);
        }
    }
}

int main(int argc, char **argv) {
#else
int glow_main(int argc, char **argv) {
#endif
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    next_free_reg = 0;

    // 1. Check for Embedded Package
    char self_path[1024];
    int len = get_self_exe_path(self_path, sizeof(self_path)-1);
    if (len != -1) {
        self_path[len] = '\0';
        FILE *f = fopen(self_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, -12, SEEK_END); // Marker is 12 bytes: "GLOW_ARCHIVE"
            char marker[13];
            if (fread(marker, 1, 12, f) == 12) {
                marker[12] = '\0';
                if (strcmp(marker, "GLOW_ARCHIVE") == 0) {
                    fseek(f, -20, SEEK_END); // Read size (long)
                    long script_size = 0;
                    if (fread(&script_size, sizeof(long), 1, f) == 1) {
                        fseek(f, fsize - 20 - script_size, SEEK_SET);
                        char *script = malloc(script_size + 1);
                        if (fread(script, 1, script_size, f) == (size_t)script_size) {
                            script[script_size] = '\0';
                            fclose(f);

                            compiling_file = "<embedded>";
                            root = NULL;
                            YY_BUFFER_STATE bp = yy_scan_string(script);
                            int parse_res = yyparse(); yy_delete_buffer(bp);
                            free(script);
                            if (parse_res != 0) {
                                std_cleanup();
                                return 1;
                            }
                            if (root) {
                                Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc;
                                main_fe.body = main_bc; compile(root);
                                main_bc->local_count = next_free_reg;
                                bc_emit(main_bc, OP_RETURN, 0, 0);
                                optimize_bytecode(main_bc);
                                pthread_mutex_lock(&glow_gil);
                                run_bytecode(main_bc, NULL, NULL, 0);
                                pthread_mutex_unlock(&glow_gil);
                                if (has_exception) print_error_trace(pending_exception);
                            }
                            std_cleanup(); return 0;
                        } else {
                            free(script);
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    if (argc == 1) {
        // Default to REPL
        goto l_REPL;
    }

    const char *subcommand = argv[1];
    if (strcmp(subcommand, "--help") == 0 || strcmp(subcommand, "-h") == 0 || strcmp(subcommand, "help") == 0) {
        printf("GlowLang v1.0 - Production Runtime\n");
        printf("Usage: glow <subcommand> [args]\n\n");
        printf("Subcommands:\n");
        printf("  run <file.glow>   Execute a GlowLang script (default)\n");
        printf("  repl              Launch the interactive development shell\n");
        printf("  check <file.glow> Perform syntax and static analysis only\n");
        printf("  trace <file.glow> Execute with frame-by-frame VM tracing\n");
        printf("  package <src>     Create a standalone executable binary\n");
        printf("  --dump-bc <file>  Dump compiled bytecode for debugging\n");
        printf("  --lsp             Start the Language Server Protocol (LSP) mode\n\n");
        printf("Options:\n");
        printf("  -o <output>       Specify output file for 'package' subcommand\n");
        return 0;
    }

    if (strcmp(subcommand, "package") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: glow package <file.glow> [-o output]\n"); return 1; }
        const char *src_file = argv[2];
        const char *out_file = "app.bin";
        if (argc > 4 && strcmp(argv[3], "-o") == 0) out_file = argv[4];

        FILE *fsrc = fopen(src_file, "rb");
        if (!fsrc) { fprintf(stderr, "Error: Could not open source '%s'\n", src_file); return 1; }
        fseek(fsrc, 0, SEEK_END); long src_size = ftell(fsrc); fseek(fsrc, 0, SEEK_SET);
        char *src_buf = malloc(src_size);
        if (fread(src_buf, 1, src_size, fsrc) != (size_t)src_size) {
            // Silence warning and handle partial read
        }
        fclose(fsrc);

        char self_path[1024];
        int self_len = get_self_exe_path(self_path, sizeof(self_path)-1);
        if (self_len == -1) { free(src_buf); return 1; }
        self_path[self_len] = '\0';
        FILE *fself = fopen(self_path, "rb");
        if (!fself) { free(src_buf); return 1; }
        
        FILE *fout = fopen(out_file, "wb");
        if (!fout) { fclose(fself); free(src_buf); return 1; }
        
        char buf[8192]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fself)) > 0) fwrite(buf, 1, n, fout);
        fclose(fself);

        fwrite(src_buf, 1, src_size, fout);
        fwrite(&src_size, sizeof(long), 1, fout);
        fwrite("GLOW_ARCHIVE", 1, 12, fout);
        fclose(fout);
        free(src_buf);
        
        char chmod_cmd[1024]; snprintf(chmod_cmd, 1024, "chmod +x %s", out_file);
        if (system(chmod_cmd) == -1) {
            // handle error
        }
        printf("Package created: %s\n", out_file);
        return 0;
    }

    if (strcmp(subcommand, "repl") == 0) {
        goto l_REPL;
    } else if (strcmp(subcommand, "check") == 0 || strcmp(subcommand, "run") == 0 || strcmp(subcommand, "trace") == 0 || strcmp(subcommand, "--dump-bc") == 0) {
        int dump_bc = 0;
        int check_only = (strcmp(subcommand, "check") == 0);
        if (strcmp(subcommand, "trace") == 0) trace_mode = 1;
        const char *filename = NULL;

        if (strcmp(subcommand, "--dump-bc") == 0) {
            dump_bc = 1;
            filename = (argc > 2) ? argv[2] : NULL;
        } else {
            filename = (argc > 2) ? argv[2] : NULL;
        }

        if (!filename && !check_only) {
            // If 'run' was used without filename, maybe it was 'glow file.glow' (old style)
            filename = subcommand;
        }

        if (filename) {
            const char *ext = strrchr(filename, '.');
            if (!ext || strcmp(ext, ".glow") != 0) {
                fprintf(stderr, "Error: GlowLang source files must have the '.glow' extension.\n");
                return 1;
            }
            yyin = fopen(filename, "r");
            if (yyin) {
                compiling_file = filename;
                root = NULL;
                int parse_res = yyparse();
                if (parse_res != 0) {
                    if (yyin) fclose(yyin);
                    std_cleanup();
                    return 1;
                }
                if (root) {
                    if (main_fe.body) bc_free(main_fe.body);
                    Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0;
                    main_fe.body = main_bc;
                    compile(root);
                    main_bc->local_count = next_free_reg;
                    bc_emit(main_bc, OP_RETURN, 0, 0);
                    optimize_bytecode(main_bc);

                    if (dump_bc) {
                        for (int i=0; i<func_table_size; i++) {
                            if (func_table[i].used && func_table[i].body) {
                                printf("Function: %s\n", func_table[i].name);
                                print_bytecode(func_table[i].body);
                            }
                        }
                        printf("Main:\n");
                        print_bytecode(main_bc);
                    }

                    if (!check_only) {
                        pthread_mutex_lock(&glow_gil);
                        run_bytecode(main_bc, NULL, NULL, 0);
                        pthread_mutex_unlock(&glow_gil);
                        if (runtime_error_thrown) {
                            std_cleanup();
                            return 1;
                        }
                    } else {
                        printf("Syntax Check: OK\n");
                    }
                }
                if (yyin) fclose(yyin);
            } else {
                fprintf(stderr, "Error: Could not open file '%s'\n", filename);
                return 1;
            }
        }
    } else if (strcmp(subcommand, "--lsp") == 0) {
        handle_client(0);
        return 0;
    } else {
        // Old style: glow file.glow
        const char *filename = subcommand;
        const char *ext = strrchr(filename, '.');
        if (!ext || strcmp(ext, ".glow") != 0) {
            fprintf(stderr, "Error: GlowLang source files must have the '.glow' extension.\n");
            return 1;
        }
        yyin = fopen(filename, "r");
        if (yyin) {
            compiling_file = filename;
            root = NULL;
            int parse_res = yyparse();
            if (parse_res != 0) {
                if (yyin) fclose(yyin);
                std_cleanup();
                return 1;
            }
            if (root) {
                if (main_fe.body) bc_free(main_fe.body);
                Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0;
                main_fe.body = main_bc;
                compile(root);
                main_bc->local_count = next_free_reg;
                bc_emit(main_bc, OP_RETURN, 0, 0);
                optimize_bytecode(main_bc);
                pthread_mutex_lock(&glow_gil);
                run_bytecode(main_bc, NULL, NULL, 0);
                pthread_mutex_unlock(&glow_gil);
                if (runtime_error_thrown) {
                    std_cleanup();
                    return 1;
                }
            }
            if (yyin) fclose(yyin);
        } else {
            fprintf(stderr, "Usage: glow [run|repl|check] <file>\n");
            return 1;
        }
    }

    std_cleanup();
    return 0;

l_REPL:
    printf("GlowLang v1.0 Interactive REPL\nType 'exit' to quit.\n");
    char line_buf[4096];
    compiling_file = "<stdin>";
    while (1) {
        printf("glow> ");
        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;
        if (strncmp(line_buf, "exit", 4) == 0) break;
        root = NULL; glow_clear_errors();
        YY_BUFFER_STATE bp = yy_scan_string(line_buf);
        yyparse(); yy_delete_buffer(bp);
        if (glow_error_count == 0 && root) {
            if (!main_fe.body) main_fe.body = bc_new(&main_fe);
            Bytecode *main_bc = main_fe.body; compiling_bc = main_bc;
            int old_count = main_bc->count;
            compile(root);
            if (main_bc->count > old_count) {
                main_bc->local_count = next_free_reg;
                int ret_idx = bc_emit(main_bc, OP_RETURN, 0, 0);
                Instruction *start_pc = &main_bc->code[old_count];
                pthread_mutex_lock(&glow_gil);
                Value res = run_bytecode(main_bc, start_pc, NULL, 0);
                pthread_mutex_unlock(&glow_gil);
                if (has_exception) { print_error_trace(pending_exception); has_exception = 0; }
                else if (res != 0 && GET_TYPE(res) != VAL_NONE) { print_value(res); printf("\n"); }
                main_bc->count = ret_idx;
            }
        }
    }
    std_cleanup();
    return 0;
}
