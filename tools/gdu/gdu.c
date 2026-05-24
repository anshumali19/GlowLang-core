#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void print_usage() {
    printf("gdu - Glow Dependency Utility\n");
    printf("Usage: gdu <command> [args]\n");
    printf("Commands:\n");
    printf("  add <url>     Add a remote dependency\n");
    printf("  install       Install dependencies from Glow.deps\n");
}

void ensure_dir(const char *dir) {
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0755);
    }
}

void calculate_hash(const char *path, char *out_hash) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        strcpy(out_hash, "00000000");
        return;
    }
    unsigned int hash = 5381;
    int c;
    while ((c = fgetc(f)) != EOF) {
        hash = ((hash << 5) + hash) + c;
    }
    fclose(f);
    sprintf(out_hash, "%08x", hash);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    char *command = argv[1];

    if (strcmp(command, "add") == 0) {
        if (argc < 3) {
            printf("Error: 'add' requires a URL\n");
            return 1;
        }
        char *url = argv[2];
        char *filename = strrchr(url, '/');
        if (filename) filename++; else filename = "module.glow";

        char local_path[1024];
        snprintf(local_path, sizeof(local_path), ".glow_pkg/%s", filename);

        printf("Fetching %s...\n", url);
        ensure_dir(".glow_pkg");
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "curl -s -L \"%s\" -o \"%s\"", url, local_path);
        int res = system(cmd);

        if (res == 0) {
            char hash[64];
            calculate_hash(local_path, hash);
            printf("Successfully added %s to %s (hash: %s)\n", url, local_path, hash);

            /* Update Glow.deps */
            int found_deps = 0;
            FILE *f = fopen("Glow.deps", "r");
            if (f) {
                char line[2048];
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, url)) { found_deps = 1; break; }
                }
                fclose(f);
            }
            if (!found_deps) {
                f = fopen("Glow.deps", "a");
                if (f) {
                    fprintf(f, "\"%s\" \"%s\"\n", url, local_path);
                    fclose(f);
                }
            }

            /* Update Glow.lock */
            int found_lock = 0;
            f = fopen("Glow.lock", "r");
            if (f) {
                char line[2048];
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, url)) { found_lock = 1; break; }
                }
                fclose(f);
            }
            if (!found_lock) {
                f = fopen("Glow.lock", "a");
                if (f) {
                    fprintf(f, "\"%s\" \"%s\" \"%s\"\n", url, hash, local_path);
                    fclose(f);
                }
            }
        } else {
            printf("Error: Failed to fetch %s (command: %s)\n", url, cmd);
        }
    } else if (strcmp(command, "install") == 0) {
        FILE *lock_f = fopen("Glow.lock", "r");
        if (lock_f) {
            printf("Installing from Glow.lock...\n");
            ensure_dir(".glow_pkg");
            char url[512], expected_hash[64], path[512];
            while (fscanf(lock_f, " \"%511[^\"]\" \"%63[^\"]\" \"%511[^\"]\"", url, expected_hash, path) == 3) {
                printf("Installing %s -> %s...\n", url, path);
                char cmd[2048];
                snprintf(cmd, sizeof(cmd), "curl -s -L \"%s\" -o \"%s\"", url, path);
                system(cmd);
                
                char actual_hash[64];
                calculate_hash(path, actual_hash);
                if (strcmp(actual_hash, expected_hash) != 0) {
                    printf("Warning: Hash mismatch for %s! Expected %s, got %s\n", url, expected_hash, actual_hash);
                } else {
                    printf("Verified hash for %s\n", url);
                }
            }
            fclose(lock_f);
        } else {
            printf("No Glow.lock found. Installing from Glow.deps and generating Glow.lock...\n");
            FILE *deps_f = fopen("Glow.deps", "r");
            if (!deps_f) {
                printf("No Glow.deps found.\n");
                return 1;
            }
            ensure_dir(".glow_pkg");
            FILE *new_lock_f = fopen("Glow.lock", "w");
            char url[512], path[512];
            while (fscanf(deps_f, " \"%511[^\"]\" \"%511[^\"]\"", url, path) == 2) {
                printf("Installing %s -> %s...\n", url, path);
                char cmd[2048];
                snprintf(cmd, sizeof(cmd), "curl -s -L \"%s\" -o \"%s\"", url, path);
                system(cmd);
                
                char hash[64];
                calculate_hash(path, hash);
                if (new_lock_f) {
                    fprintf(new_lock_f, "\"%s\" \"%s\" \"%s\"\n", url, hash, path);
                }
            }
            fclose(deps_f);
            if (new_lock_f) fclose(new_lock_f);
        }
    } else {
        printf("Unknown command: %s\n", command);
        print_usage();
        return 1;
    }

    return 0;
}
