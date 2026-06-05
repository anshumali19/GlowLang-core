/*
 * GlowLang Language Server Protocol (LSP) Implementation
 * ======================================================
 * JSON-RPC 2.0 based LSP server supporting:
 *   - textDocument/completion (keywords, builtins, modules)
 *   - textDocument/hover (documentation)
 *   - textDocument/definition (goto definition)
 *   - textDocument/publishDiagnostics (syntax errors)
 *   - textDocument/semanticTokens/full (syntax coloring)
 *
 * Runs over stdin/stdout via `glowlang --lsp`
 */

#ifndef LSP_H
#define LSP_H

#include "glowlang.h"

/* ================================================================
 *  DOCUMENT STORE
 * ================================================================ */
#define LSP_MAX_DOCS       64
#define LSP_MAX_URI        1024
#define LSP_MAX_DOC_SIZE   (1024 * 1024)  /* 1 MB per document */

typedef struct {
    char  uri[LSP_MAX_URI];
    char *text;
    int   text_len;
    int   version;
} LspDocument;

typedef struct {
    LspDocument docs[LSP_MAX_DOCS];
    int         count;
} LspDocStore;

/* ================================================================
 *  SEMANTIC TOKEN TYPES
 * ================================================================ */
enum {
    TOK_KEYWORD   = 0,
    TOK_FUNCTION  = 1,
    TOK_VARIABLE  = 2,
    TOK_STRING    = 3,
    TOK_NUMBER    = 4,
    TOK_COMMENT   = 5,
    TOK_OPERATOR  = 6,
    TOK_CLASS     = 7,
    TOK_TYPE      = 8
};

/* ================================================================
 *  PUBLIC API
 * ================================================================ */

/* Run the LSP server over stdin/stdout (main entry point) */
void lsp_run_stdio(void);

/* Legacy: handle a single client over a file descriptor */
void handle_client(int fd);

#endif /* LSP_H */
