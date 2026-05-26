#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("Usage: gdu <command> [args]\n"); return 1; }
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { printf("Usage: gdu install <pkg>\n"); return 1; }
        char cmd[256];
        sprintf(cmd, "curl -s https://glowlang.org/pkgs/%s.glow -o .glow_pkg/%s.glow", argv[2], argv[2]);
        if (system(cmd) < 0) {}
        printf("Installed package: %s\n", argv[2]);
    } else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) { printf("Usage: gdu run <file>\n"); return 1; }
        char cmd[256];
        sprintf(cmd, "bin/glowlang %s", argv[2]);
        if (system(cmd) < 0) {}
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }
    return 0;
}
