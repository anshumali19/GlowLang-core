#include "lsp.h"
#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Flex functions for buffer parsing */
typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

static void send_response_fd(int fd, const char *json) {
    int len = strlen(json);
    char header[128];
    int hlen = sprintf(header, "Content-Length: %d\r\n\r\n", len);
    write(fd, header, hlen);
    write(fd, json, len);
}

static void send_diagnostics_fd(int fd, const char *uri) {
    char buf[4096];
    int pos = 0;
    pos += sprintf(buf + pos, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"diagnostics\":[", uri);
    
    for (int i = 0; i < glow_error_count; i++) {
        pos += sprintf(buf + pos, "{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":100}},\"severity\":1,\"message\":\"%s\"}", 
            glow_errors[i].line - 1, glow_errors[i].line - 1, glow_errors[i].message);
        if (i < glow_error_count - 1) pos += sprintf(buf + pos, ",");
    }
    
    pos += sprintf(buf + pos, "]}}");
    send_response_fd(fd, buf);
}

static int read_exact_fd(int fd, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int r = read(fd, buf + total, len - total);
        if (r <= 0) return total;
        total += r;
    }
    return total;
}

static void handle_client(int fd) {
    char header[1024];
    fprintf(stderr, "LSP: Handling client on fd %d\n", fd);
    
    while (1) {
        int content_len = -1;
        while (1) {
            int hpos = 0;
            while (hpos < sizeof(header) - 1) {
                char c;
                if (read(fd, &c, 1) <= 0) {
                    fprintf(stderr, "LSP: Client disconnected\n");
                    return;
                }
                header[hpos++] = c;
                if (c == '\n') break;
            }
            header[hpos] = '\0';
            if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) break;
            if (strncmp(header, "Content-Length: ", 16) == 0) {
                content_len = atoi(header + 16);
            }
        }
        
        if (content_len > 0) {
            char *body = malloc(content_len + 1);
            read_exact_fd(fd, body, content_len);
            body[content_len] = '\0';
            
            if (strstr(body, "\"method\":\"initialize\"")) {
                send_response_fd(fd, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"capabilities\":{\"textDocumentSync\":1}}}");
            } else if (strstr(body, "\"method\":\"textDocument/didOpen\"") || strstr(body, "\"method\":\"textDocument/didChange\"")) {
                char *uri_start = strstr(body, "\"uri\":\"");
                if (uri_start) {
                    uri_start += 7;
                    char *uri_end = strstr(uri_start, "\"");
                    if (uri_end) {
                        char *uri = strndup(uri_start, uri_end - uri_start);
                        char *text_start = strstr(body, "\"text\":\"");
                        if (text_start) {
                            text_start += 8;
                            char *text_end = strstr(text_start, "\",");
                            if (!text_end) text_end = strstr(text_start, "\"}");
                            if (!text_end) text_end = strrchr(text_start, '"');
                            if (text_end) {
                                char *text = strndup(text_start, text_end - text_start);
                                char *unescaped = malloc(strlen(text) + 1);
                                char *src = text, *dst = unescaped;
                                while (*src) {
                                    if (*src == '\\' && *(src+1) == 'n') { *dst++ = '\n'; src += 2; }
                                    else if (*src == '\\' && *(src+1) == '"') { *dst++ = '"'; src += 2; }
                                    else if (*src == '\\' && *(src+1) == '\\') { *dst++ = '\\'; src += 2; }
                                    else { *dst++ = *src++; }
                                }
                                *dst = '\0';
                                glow_clear_errors();
                                YY_BUFFER_STATE buffer = yy_scan_string(unescaped);
                                line_num = 1;
                                yyparse();
                                yy_delete_buffer(buffer);
                                send_diagnostics_fd(fd, uri);
                                free(text);
                                free(unescaped);
                            }
                        }
                        free(uri);
                    }
                }
            }
            free(body);
        }
    }
}

void lsp_run(void) {
    handle_client(0);
}

void lsp_run_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return;
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return;
    }
    
    fprintf(stderr, "LSP: Listening on port %d\n", port);
    
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        handle_client(client_fd);
        close(client_fd);
    }
}
