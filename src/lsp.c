#include "lsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void glow_clear_errors(void);
extern int glow_error_count;
extern int yyparse(void);

void send_response_fd(int fd, const char *json) {
    size_t len = strlen(json);
    char header[128];
    int hlen = sprintf(header, "Content-Length: %zu\r\n\r\n", len);
    if (write(fd, header, hlen) < 0) {}
    if (write(fd, json, len) < 0) {}
}

void handle_client(int fd) {
    char buffer[4096];
    while (1) {
        ssize_t bytes = read(fd, buffer, sizeof(buffer)-1);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        glow_clear_errors();
        yyparse();
        char resp[8192];
        sprintf(resp, "{\"id\": 1, \"result\": {\"errors\": %d}}", glow_error_count);
        send_response_fd(fd, resp);
    }
    close(fd);
}
